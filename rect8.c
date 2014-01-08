/*
 * rect8.c - PM VNC Viewer, Pixel Handling for bgr233 8bit static color
 *
 * May 2009 - Add Dmitry Steklenev change:
 *    On SMP systems PMVNC thread tried to access to the hpsBitmap
 *    simultaneously. Serialized access via mutex to fix
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INCL_PM
#define INCL_DOS
#include <os2.h>

#include "pmvncdef.h"

/*
 * rbuff is used to parse CoRRE, it requires buffer of
 * 255 * 255 * 32 bits (nearly 64KB * 4).
 */

#define RBUFSIZ     (1024 * 256)

static  PUCHAR  rbuff = NULL ;          /* as recv buffer   */

/*
 * buffer for bitmap scan conversion
 */

#define MAXSCAN     (4096 * 8)

static  PUCHAR  ibuff = NULL ;          /* as scan buffer   */

/*
 * minimum I/O unit min(MAXSCAN, RBUFSIZ)
 */

#define MINBUF  MAXSCAN

/*
 * Remote Frame Buffer
 */

static  int     cxBitmap = 0 ;
static  int     cyBitmap = 0 ;

static  HDC     hdcBitmap = NULLHANDLE ;
static  HPS     hpsBitmap = NULLHANDLE ;
static  HBITMAP hbmBitmap = NULLHANDLE ;
static  HMTX    hmutex    = NULLHANDLE ;

static  PBITMAPINFO2    pbmiBitmap = NULL ;

/*
 * ColorMap for BGR233
 */

static  PLONG  bgrMap ;

/*
 * rectDone - finialize rectangle operation
 */

static  void    rectDone(void)
{
    if (hbmBitmap != NULLHANDLE) {
        GpiDeleteBitmap(hbmBitmap) ;
        hbmBitmap = NULLHANDLE ;
    }
    if (hpsBitmap != NULLHANDLE) {
        GpiDestroyPS(hpsBitmap) ;
        hpsBitmap = NULLHANDLE ;
    }
    if (hdcBitmap != NULLHANDLE) {
        DevCloseDC(hdcBitmap) ;
        hdcBitmap = NULLHANDLE ;
    }
    if (rbuff != NULL) {
        free(rbuff) ;
        rbuff = NULL ;
    }
    if (ibuff != NULL) {
        free(ibuff) ;
        ibuff = NULL ;
    }
    if (pbmiBitmap != NULL) {
        free(pbmiBitmap) ;
        pbmiBitmap = NULL ;
    }
    if (hmutex != NULLHANDLE) {
        DosCloseMutexSem( hmutex ) ;
        hmutex = NULLHANDLE ;
    }
}

/*
 * rectInit - create bitmap for Remote Frame Buffer
 */

static  BOOL    rectInit(int cx, int cy)
{
    SIZEL   siz ;
    POINTL  pt  ;
    BITMAPINFOHEADER2   bmi ;
    int     i   ;
    PUCHAR  pc  ;
    PULONG  pl  ;

    if (DosCreateMutexSem( NULL, &hmutex, 0, 0 ) != 0) {
        rectDone() ;
        return FALSE ;
    }

    /*
     * prepare buffer for drawing
     */

    rbuff = malloc(RBUFSIZ) ;
    ibuff = malloc(MAXSCAN) ;
    pbmiBitmap = malloc(16 + sizeof(RGB2) * 256) ;

    if (rbuff == NULL || ibuff == NULL || pbmiBitmap == NULL) {
        rectDone() ;
        return FALSE ;
    }

    /*
     * create bitmap for Remote Frame Buffer
     */

    siz.cx = siz.cy = 0 ;
    hdcBitmap = DevOpenDC(habNetwork, OD_MEMORY, "*", 0, NULL, NULLHANDLE) ;
    hpsBitmap = GpiCreatePS(habNetwork, hdcBitmap, &siz,
                    PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC) ;

    if (hdcBitmap == NULLHANDLE || hpsBitmap == NULLHANDLE) {

#ifdef DEBUG
        TRACE("rectInit - failed to create HDC/HPS\n") ;
#endif

        rectDone() ;
        return FALSE ;
    }

    memset(&bmi, 0, sizeof(bmi)) ;
    bmi.cbFix = sizeof(bmi) ;
    bmi.cx = cxBitmap = cx ;
    bmi.cy = cyBitmap = cy ;
    bmi.cPlanes       = 1  ;
    bmi.cBitCount     = 8  ;
    bmi.ulCompression = 0  ;
    bmi.cclrUsed      = 0  ;
    bmi.cclrImportant = 0  ;

    hbmBitmap = GpiCreateBitmap(hpsBitmap, &bmi, 0, NULL, NULL) ;

    if (hbmBitmap == NULLHANDLE) {

#ifdef DEBUG
        TRACE("rectInit - failed to create bitmap\n") ;
#endif

        rectDone() ;
        return FALSE ;
    }

    GpiSetBitmap(hpsBitmap, hbmBitmap) ;

    /*
     * put initial message on it
     */

    pt.x = 32 ;
    pt.y = cyBitmap - 64 ;
    GpiErase(hpsBitmap) ;
    GpiCharStringAt(hpsBitmap, &pt, strlen(VncGreeting), VncGreeting) ;

    if (GpiCreateLogColorTable(hpsBitmap, 0, LCOLF_CONSECRGB, 0, 256, bgrMap) != TRUE) {

#ifdef DEBUG
        TRACE("rectInit - failed to set color mode INDEX\n") ;
#endif

        rectDone() ;
        return FALSE ;
    }

    /*
     * prepare bitmap info. header for later operation
     */

    memset(pbmiBitmap, 0, (16 + sizeof(RGB2) * 256)) ;
    pbmiBitmap->cbFix     = 16 ;
    pbmiBitmap->cPlanes   =  1 ;
    pbmiBitmap->cBitCount =  8 ;
    pc = (PUCHAR) pbmiBitmap ;
    pl = (PULONG) (pc + 16)  ;

    for (i = 0 ; i < 256 ; i++) {
        *pl++ = bgrMap[i] ;
    }

    return TRUE ;
}

/*
 * rectDraw - draw bitmap to targte PS
 */

static  BOOL    rectDraw(HPS hps, PPOINTL apt)
{
    if (hpsBitmap == NULLHANDLE) {
        return FALSE ;
    }
    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ;
    GpiBitBlt(hps, hpsBitmap, 4, apt, ROP_SRCCOPY, BBO_IGNORE) ;
    DosReleaseMutexSem( hmutex ) ;
    return TRUE ;
}

/*
 * rectRaw - raw rectangle encoding
 *      read RawRect data from Server and convert to PM Bitmap
 */

static  void    rawConv(rfbRectangle *r, int lines, PUCHAR buff)
{
    PUCHAR  sp, dp ;
    int     i, j   ;
    int     bytesPerLine ;
    POINTL  apt[4] ;

    /*
     * convert to PM bitmap, reverse vertical order and align to ULONG
     */

    bytesPerLine = (r->w + 3) & ~0x03 ;

    for (i = 0, sp = buff ; i < lines ; i++) {
        dp = &ibuff[bytesPerLine * (lines - i - 1)] ;
        for (j = 0 ; j < r->w ; j++) {
            *dp++ = *sp++ ;
        }
    }

    pbmiBitmap->cx = r->w  ;
    pbmiBitmap->cy = lines ;

    apt[0].x = r->x ;
    apt[0].y = (cyBitmap - r->y - lines) ;
    apt[1].x = apt[0].x + r->w  - 1 ;
    apt[1].y = apt[0].y + lines - 1 ;
    apt[2].x = 0 ;
    apt[2].y = 0 ;
    apt[3].x = r->w  ;
    apt[3].y = lines ;

    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ;
    GpiDrawBits(hpsBitmap, ibuff, pbmiBitmap, 4, apt, ROP_SRCCOPY, 0) ;
    DosReleaseMutexSem( hmutex ) ;
}

static  BOOL    rectRaw(rfbRectangle *r)
{
    int     bytesPerLine, linesToRead ;

    bytesPerLine  = r->w ;          /* for bgr233 specific  */
    linesToRead   = MINBUF / ((r->w + 3) & ~0x03) ;

    if (linesToRead == 0) {
        netFail("rectRaw - scanline too large, recompile!!") ;
        return FALSE ;
    }

    while (r->h > 0) {
        if (linesToRead > r->h) {
            linesToRead = r->h ;
        }
        if (netRecv(rbuff, bytesPerLine * linesToRead) != TRUE) {
            netFail("failed to recv Raw Scanline") ;
            return FALSE ;
        }
        rawConv(r, linesToRead, rbuff) ;
        r->h -= linesToRead ;
        r->y += linesToRead ;
    }
    return TRUE ;
}

/*
 * rectCopy - copy rect encoding
 */

static  BOOL    rectCopy(rfbRectangle *r)
{
    rfbCopyRect cr  ;
    POINTL  apt[3]  ;

    if (netRecv((PUCHAR) &cr, sz_rfbCopyRect) != TRUE) {
        netFail("failed to recv CopyRect") ;
        return FALSE ;
    }

    cr.srcX = swap16(cr.srcX) ;
    cr.srcY = swap16(cr.srcY) ;

    apt[0].x = r->x                      ;
    apt[0].y = cyBitmap - r->y - r->h    ;
    apt[1].x = r->x + r->w               ;
    apt[1].y = cyBitmap - r->y           ;
    apt[2].x = cr.srcX                   ;
    apt[2].y = cyBitmap - cr.srcY - r->h ;

    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ;
    GpiBitBlt(hpsBitmap, hpsBitmap, 3, apt, ROP_SRCCOPY, 0) ;
    DosReleaseMutexSem( hmutex ) ;

    return TRUE ;
}

/*
 * Fill Rectangle with Solid Color
 */

#define     fillRect(mx, my, mw, mh, color)         \
{                                                   \
    POLYGON pg ;                                    \
    POINTL  apt[8] ;                                \
    int     n, i ;                                  \
    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ; \
    GpiSetColor(hpsBitmap, (LONG) (color)) ;        \
    if ((mw) == 1 && (mh) == 1) {                   \
        apt[0].x = (mx)                   ;         \
        apt[0].y = cyBitmap - (my) - (mh) ;         \
        GpiSetPel(hpsBitmap, &apt[0])     ;         \
    } else if ((mw) <  4) {                         \
        for (i = 0, n = 0 ; i < (mw) ; i++) {       \
            apt[n].x = (mx) + i               ;     \
            apt[n].y = cyBitmap - (my) - (mh) ;     \
            n += 1                            ;     \
            apt[n].x = (mx) + i               ;     \
            apt[n].y = cyBitmap - (my) - 1    ;     \
            n += 1                            ;     \
        }                                           \
        GpiPolyLineDisjoint(hpsBitmap, n, apt) ;    \
    } else {                                        \
        pg.ulPoints = 4  ;                              \
        pg.aPointl = apt ;                              \
        apt[2].x = apt[3].x = (mx)                   ;  \
        apt[0].x = apt[1].x = (mx) + (mw) - 1        ;  \
        apt[0].y = apt[3].y = cyBitmap - (my) - (mh) ;  \
        apt[1].y = apt[2].y = cyBitmap - (my) - 1    ;  \
        GpiMove(hpsBitmap, &apt[3])          ;          \
        GpiPolygons(hpsBitmap, 1, &pg, 0, 0) ;          \
    }                                                   \
    DosReleaseMutexSem( hmutex ) ;                      \
}

/*
 * rectRRE - RRE encoding
 */

static  BOOL    rectRRE(rfbRectangle *r)
{
    rfbRREHeader    hdr ;
    rfbRectangle    subrect ;
    CARD8   color    ;
    int     i        ;

    if (netRecv((PUCHAR) &hdr, sz_rfbRREHeader) != TRUE) {
        netFail("failed to recv RREHeader") ;
        return FALSE ;
    }
    hdr.nSubrects = swap32(hdr.nSubrects) ;

    if (netRecv((PUCHAR) &color, sizeof(CARD8)) != TRUE) {
        netFail("failed to recv BG color") ;
        return FALSE ;
    }

    fillRect(r->x, r->y, r->w, r->h, color) ;

    for (i = 0 ; i < hdr.nSubrects ; i++) {
        if (netRecv((PUCHAR) &color, sizeof(CARD8)) != TRUE) {
            netFail("failed to recv sub color") ;
            return FALSE ;
        }
        if (netRecv((PUCHAR) &subrect, sz_rfbRectangle) != TRUE) {
            netFail("failed to recv subrect") ;
            return FALSE ;
        }

        subrect.x = r->x + swap16(subrect.x) ;
        subrect.y = r->y + swap16(subrect.y) ;
        subrect.w = swap16(subrect.w) ;
        subrect.h = swap16(subrect.h) ;

        fillRect(subrect.x, subrect.y, subrect.w, subrect.h, color) ;
    }
    return TRUE ;
}

/*
 * rectCoRRE - CoRRE encoding
 */

static  BOOL    rectCoRRE(rfbRectangle *r)
{
    rfbRREHeader    hdr ;
    rfbRectangle    subrect ;
    PUCHAR  sp       ;
    CARD8   color    ;
    int     i        ;

    if (netRecv((PUCHAR) &hdr, sz_rfbRREHeader) != TRUE) {
        netFail("failed to recv RREHeader") ;
        return FALSE ;
    }
    hdr.nSubrects = swap32(hdr.nSubrects) ;

    if (netRecv((PUCHAR) &color, sizeof(CARD8)) != TRUE) {
        netFail("failed to recv BG color") ;
        return FALSE ;
    }

    fillRect(r->x, r->y, r->w, r->h, color) ;

    if (netRecv(rbuff, hdr.nSubrects * 5) != TRUE) {
        netFail("failed to recv subrects") ;
        return FALSE ;
    }

    for (i = 0, sp = rbuff ; i < hdr.nSubrects ; i++, sp += 5) {
        color = *(CARD8 *) sp ;
        subrect.x = r->x + sp[1] ;
        subrect.y = r->y + sp[2] ;
        subrect.w = sp[3] ;
        subrect.h = sp[4] ;

        fillRect(subrect.x, subrect.y, subrect.w, subrect.h, color) ;
    }
    return TRUE ;
}

/*
 * rectTile - Hextile encoding
 */

static  CARD8   tileFg = 0xff ;
static  CARD8   tileBg = 0x00 ;

static  PUCHAR  tileScan[16] ;
static  POINTL  tileBlit[4]  ;

static  void    hextileInit(int x, int y, int w, int h, CARD8 bg)
{
    int     i, bytesPerLine ;

    /*
     * fill tile with background color
     */

    bytesPerLine = (w + 3) & ~0x03 ;
    memset(ibuff, bg, (bytesPerLine * h)) ;

    for (i = 0 ; i < h  ; i++) {
        tileScan[i] = &ibuff[bytesPerLine * (h - i - 1)] ;
    }

    /*
     * prepare for final bitblt
     */

    pbmiBitmap->cx = w ;
    pbmiBitmap->cy = h ;

    tileBlit[0].x = x ;
    tileBlit[0].y = (cyBitmap - y - h) ;
    tileBlit[1].x = tileBlit[0].x + w  - 1 ;
    tileBlit[1].y = tileBlit[0].y + h - 1 ;
    tileBlit[2].x = 0 ;
    tileBlit[2].y = 0 ;
    tileBlit[3].x = w ;
    tileBlit[3].y = h ;
}

#define hextileDraw()   \
    GpiDrawBits(hpsBitmap, ibuff, pbmiBitmap, 4, tileBlit, ROP_SRCCOPY, 0)

#define hextileFill(x, y, w, h, color)      \
{                                           \
    int     i ;                             \
    PUCHAR  p ;                             \
    if ((w) == 1) {                         \
        for (i = 0 ; i < (h) ; i++) {       \
            p = tileScan[(y) + i] + (x) ;   \
            *p = (color) ;                  \
        }                                   \
    } else {                                \
        for (i = 0 ; i < (h) ; i++) {       \
            p = tileScan[(y) + i] + (x) ;   \
            memset(p, (color), (w)) ;       \
        }                                   \
    }                                       \
}

static  BOOL    rectTile(rfbRectangle *r)
{
    CARD8   subencoding, nSubrects ;
    int     x, y, w, h ;
    int     i, j, bytesPerLine ;
    int     sn, sx, sy, sw, sh ;
    PUCHAR  sp, dp ;
    POINTL  apt[4] ;

    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ;

    for (y = r->y ; y < (r->y + r->h) ; y += 16) {
        for (x = r->x ; x < (r->x + r->w) ; x += 16) {
            w = h = 16 ;
            if ((r->x + r->w - x) < 16) {
                w = r->x + r->w - x ;
            }
            if ((r->y + r->h - y) < 16) {
                h = r->y + r->h - y ;
            }
            if (netRecv((PUCHAR) &subencoding, 1) != TRUE) {
                DosReleaseMutexSem( hmutex ) ;
                netFail("failed to recv sub encoding") ;
                return FALSE ;
            }
            if (subencoding & rfbHextileRaw) {

                bytesPerLine = (w + 3) & ~0x03 ;

                if (netRecv(rbuff, w * h) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed on hextile raw") ;
                    return FALSE ;
                }
                for (i = 0, sp = rbuff ; i < h ; i++) {
                    dp = &ibuff[bytesPerLine * (h - i - 1)] ;
                    for (j = 0 ; j < w ; j++) {
                        *dp++ = *sp++ ;
                    }
                }

                pbmiBitmap->cx = w ;
                pbmiBitmap->cy = h ;

                apt[0].x = x ;
                apt[0].y = (cyBitmap - y - h) ;
                apt[1].x = apt[0].x + w  - 1 ;
                apt[1].y = apt[0].y + h - 1 ;
                apt[2].x = 0 ;
                apt[2].y = 0 ;
                apt[3].x = w ;
                apt[3].y = h ;

                GpiDrawBits(hpsBitmap, ibuff,
                                    pbmiBitmap, 4, apt, ROP_SRCCOPY, 0) ;
                continue ;
            }
            if (subencoding & rfbHextileBackgroundSpecified) {
                if (netRecv((PUCHAR) &tileBg, sizeof(CARD8)) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv BG color") ;
                    return FALSE ;
                }
            }
            if (subencoding & rfbHextileForegroundSpecified) {
                if (netRecv((PUCHAR) &tileFg, sizeof(CARD8)) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv FG color") ;
                    return FALSE ;
                }
            }
            if ((subencoding & rfbHextileAnySubrects) == 0) {
                hextileInit(x, y, w, h, tileBg) ;
                hextileDraw() ;
            } else if (subencoding & rfbHextileSubrectsColoured) {
                hextileInit(x, y, w, h, tileBg) ;
                if (netRecv((PUCHAR) &nSubrects, 1) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv hextile nSubrects") ;
                    return FALSE ;
                }
                if (netRecv((PUCHAR) rbuff, nSubrects * 3) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv hextile Subrects") ;
                    return FALSE ;
                }

                for (sn = 0, sp = rbuff ; sn < nSubrects ; sn++, sp += 3) {
                    sx = rfbHextileExtractX(sp[1]) ;
                    sy = rfbHextileExtractY(sp[1]) ;
                    sw = rfbHextileExtractW(sp[2]) ;
                    sh = rfbHextileExtractH(sp[2]) ;
                    hextileFill(sx, sy, sw, sh, sp[0]) ;
                }
                hextileDraw() ;
            } else {
                hextileInit(x, y, w, h, tileBg) ;
                if (netRecv((PUCHAR) &nSubrects, 1) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv hextile nSubrects") ;
                    return FALSE ;
                }
                if (netRecv((PUCHAR) rbuff, nSubrects * 2) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv hextile Subrects") ;
                    return FALSE ;
                }

                for (sn = 0, sp = rbuff ; sn < nSubrects ; sn++, sp += 2) {
                    sx = rfbHextileExtractX(sp[0]) ;
                    sy = rfbHextileExtractY(sp[0]) ;
                    sw = rfbHextileExtractW(sp[1]) ;
                    sh = rfbHextileExtractH(sp[1]) ;
                    hextileFill(sx, sy, sw, sh, tileFg) ;
                }
                hextileDraw() ;
            }
        }
    }
    DosReleaseMutexSem( hmutex ) ;
    return TRUE ;
}

/*
 * VncPix8 - Drawing Context for bgr233 8bit static color
 */

static LONG normMap[] = {
    0x00000000, 0x00240000, 0x00480000, 0x006d0000,
    0x00910000, 0x00b60000, 0x00da0000, 0x00ff0000,
    0x00002400, 0x00242400, 0x00482400, 0x006d2400,
    0x00912400, 0x00b62400, 0x00da2400, 0x00ff2400,
    0x00004800, 0x00244800, 0x00484800, 0x006d4800,
    0x00914800, 0x00b64800, 0x00da4800, 0x00ff4800,
    0x00006d00, 0x00246d00, 0x00486d00, 0x006d6d00,
    0x00916d00, 0x00b66d00, 0x00da6d00, 0x00ff6d00,
    0x00009100, 0x00249100, 0x00489100, 0x006d9100,
    0x00919100, 0x00b69100, 0x00da9100, 0x00ff9100,
    0x0000b600, 0x0024b600, 0x0048b600, 0x006db600,
    0x0091b600, 0x00b6b600, 0x00dab600, 0x00ffb600,
    0x0000da00, 0x0024da00, 0x0048da00, 0x006dda00,
    0x0091da00, 0x00b6da00, 0x00dada00, 0x00ffda00,
    0x0000ff00, 0x0024ff00, 0x0048ff00, 0x006dff00,
    0x0091ff00, 0x00b6ff00, 0x00daff00, 0x00ffff00,
    0x00000055, 0x00240055, 0x00480055, 0x006d0055,
    0x00910055, 0x00b60055, 0x00da0055, 0x00ff0055,
    0x00002455, 0x00242455, 0x00482455, 0x006d2455,
    0x00912455, 0x00b62455, 0x00da2455, 0x00ff2455,
    0x00004855, 0x00244855, 0x00484855, 0x006d4855,
    0x00914855, 0x00b64855, 0x00da4855, 0x00ff4855,
    0x00006d55, 0x00246d55, 0x00486d55, 0x006d6d55,
    0x00916d55, 0x00b66d55, 0x00da6d55, 0x00ff6d55,
    0x00009155, 0x00249155, 0x00489155, 0x006d9155,
    0x00919155, 0x00b69155, 0x00da9155, 0x00ff9155,
    0x0000b655, 0x0024b655, 0x0048b655, 0x006db655,
    0x0091b655, 0x00b6b655, 0x00dab655, 0x00ffb655,
    0x0000da55, 0x0024da55, 0x0048da55, 0x006dda55,
    0x0091da55, 0x00b6da55, 0x00dada55, 0x00ffda55,
    0x0000ff55, 0x0024ff55, 0x0048ff55, 0x006dff55,
    0x0091ff55, 0x00b6ff55, 0x00daff55, 0x00ffff55,
    0x000000aa, 0x002400aa, 0x004800aa, 0x006d00aa,
    0x009100aa, 0x00b600aa, 0x00da00aa, 0x00ff00aa,
    0x000024aa, 0x002424aa, 0x004824aa, 0x006d24aa,
    0x009124aa, 0x00b624aa, 0x00da24aa, 0x00ff24aa,
    0x000048aa, 0x002448aa, 0x004848aa, 0x006d48aa,
    0x009148aa, 0x00b648aa, 0x00da48aa, 0x00ff48aa,
    0x00006daa, 0x00246daa, 0x00486daa, 0x006d6daa,
    0x00916daa, 0x00b66daa, 0x00da6daa, 0x00ff6daa,
    0x000091aa, 0x002491aa, 0x004891aa, 0x006d91aa,
    0x009191aa, 0x00b691aa, 0x00da91aa, 0x00ff91aa,
    0x0000b6aa, 0x0024b6aa, 0x0048b6aa, 0x006db6aa,
    0x0091b6aa, 0x00b6b6aa, 0x00dab6aa, 0x00ffb6aa,
    0x0000daaa, 0x0024daaa, 0x0048daaa, 0x006ddaaa,
    0x0091daaa, 0x00b6daaa, 0x00dadaaa, 0x00ffdaaa,
    0x0000ffaa, 0x0024ffaa, 0x0048ffaa, 0x006dffaa,
    0x0091ffaa, 0x00b6ffaa, 0x00daffaa, 0x00ffffaa,
    0x000000ff, 0x002400ff, 0x004800ff, 0x006d00ff,
    0x009100ff, 0x00b600ff, 0x00da00ff, 0x00ff00ff,
    0x000024ff, 0x002424ff, 0x004824ff, 0x006d24ff,
    0x009124ff, 0x00b624ff, 0x00da24ff, 0x00ff24ff,
    0x000048ff, 0x002448ff, 0x004848ff, 0x006d48ff,
    0x009148ff, 0x00b648ff, 0x00da48ff, 0x00ff48ff,
    0x00006dff, 0x00246dff, 0x00486dff, 0x006d6dff,
    0x00916dff, 0x00b66dff, 0x00da6dff, 0x00ff6dff,
    0x000091ff, 0x002491ff, 0x004891ff, 0x006d91ff,
    0x009191ff, 0x00b691ff, 0x00da91ff, 0x00ff91ff,
    0x0000b6ff, 0x0024b6ff, 0x0048b6ff, 0x006db6ff,
    0x0091b6ff, 0x00b6b6ff, 0x00dab6ff, 0x00ffb6ff,
    0x0000daff, 0x0024daff, 0x0048daff, 0x006ddaff,
    0x0091daff, 0x00b6daff, 0x00dadaff, 0x00ffdaff,
    0x0000ffff, 0x0024ffff, 0x0048ffff, 0x006dffff,
    0x0091ffff, 0x00b6ffff, 0x00daffff, 0x00ffffff
} ;

static  BOOL    rectInitNorm(int cx, int cy)
{
    bgrMap = normMap ;

    return rectInit(cx, cy) ;
}

VNCREC      VncCtx8 = {
    /* bitsPerPixel */  8,          /* Current pixel format will fit    */
    /* depth        */  8,          /* to PM's RGB structure with no    */
    /* bigEndian    */  0,          /* conversions.                     */
    /* trueColour   */  1,          /* It reduces client side load.     */
    /* redMax       */  0x0007,     /* But for reduce network traffic,  */
    /* greenMax     */  0x0007,     /* 8 bits BGR233 will be nice.      */
    /* blureMax     */  0x0003,
    /* redShift     */  0,
    /* greenShift   */  3,
    /* blueShift    */  6,
    /* pad1, pad2   */  0, 0,
    /* rectDone     */  rectDone,
    /* rectInit     */  rectInitNorm,
    /* rectDraw     */  rectDraw,
    /* rectRaw      */  rectRaw,
    /* rectCopy     */  rectCopy,
    /* rectRRE      */  rectRRE,
    /* rectCoRRE    */  rectCoRRE,
    /* rectTile     */  rectTile
} ;

/*
 * VncPix8Tiny - Drawing Context for bgr233 mapped to default 16 colors
 */

static LONG tinyMap[] = {
    0x00000000, 0x00800000, 0x00800000, 0x00800000,
    0x00800000, 0x00800000, 0x00ff0000, 0x00ff0000,
    0x00008000, 0x00800080, 0x00800000, 0x00800000,
    0x00800000, 0x00800000, 0x00ff0000, 0x00ff0000,
    0x00008000, 0x00008000, 0x00808000, 0x00800080,
    0x00800000, 0x00800000, 0x00ff0000, 0x00ff0000,
    0x00008000, 0x00008000, 0x00008000, 0x00808000,
    0x00808000, 0x00808000, 0x00ff0000, 0x00ff0000,
    0x00008000, 0x00008000, 0x00008000, 0x00808000,
    0x00808000, 0x00808000, 0x00ff00ff, 0x00ff00ff,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00,
    0x0000ff00, 0x0000ffff, 0x00ffff00, 0x00ffff00,
    0x00000080, 0x00000080, 0x00800080, 0x00800080,
    0x00800000, 0x00800000, 0x00ff0000, 0x00ff0000,
    0x00000080, 0x00000080, 0x00800080, 0x00800080,
    0x00800000, 0x00800000, 0x00ff0000, 0x00ff0000,
    0x00008080, 0x00008080, 0x00800080, 0x00800080,
    0x00800080, 0x00ff0000, 0x00ff0000, 0x00ff0000,
    0x00008080, 0x00008080, 0x00008080, 0x00808000,
    0x00808000, 0x00808000, 0x00ff0000, 0x00ff00ff,
    0x00008000, 0x00008000, 0x00008080, 0x00808080,
    0x00808080, 0x00808080, 0x00ff00ff, 0x00ff00ff,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00ff00ff, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00,
    0x0000ffff, 0x0000ffff, 0x00ffff00, 0x00ffff00,
    0x00000080, 0x00000080, 0x00000080, 0x00800080,
    0x00800080, 0x00800080, 0x00ff0000, 0x00ff0000,
    0x00000080, 0x00000080, 0x00000080, 0x00800080,
    0x00800080, 0x00800080, 0x00ff0000, 0x00ff0000,
    0x000000ff, 0x000000ff, 0x000000ff, 0x00800080,
    0x00800080, 0x00800080, 0x00ff00ff, 0x00ff00ff,
    0x00008080, 0x00008080, 0x00008080, 0x00808080,
    0x00808080, 0x00808080, 0x00ff00ff, 0x00ff00ff,
    0x00008080, 0x00008080, 0x00008080, 0x00808080,
    0x00808080, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00ff00ff, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x0000ff00,
    0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ffff,
    0x0000ffff, 0x0000ffff, 0x00ffff00, 0x00ffff00,
    0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
    0x000000ff, 0x00808080, 0x00ff00ff, 0x00ff00ff,
    0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
    0x000000ff, 0x00808080, 0x00ff00ff, 0x00ff00ff,
    0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
    0x00808080, 0x00808080, 0x00ff00ff, 0x00ff00ff,
    0x000000ff, 0x000000ff, 0x000000ff, 0x00808080,
    0x00808080, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,
    0x00008080, 0x00008080, 0x00808080, 0x00808080,
    0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x0000ffff, 0x0000ffff,
    0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff,
    0x0000ffff, 0x00ffff00, 0x00ffff00, 0x00ffffff
} ;

static  BOOL    rectInitTiny(int cx, int cy)
{
    bgrMap = tinyMap ;

    return rectInit(cx, cy) ;
}

VNCREC      VncCtx8Tiny = {
    /* bitsPerPixel */  8,          /* Current pixel format will fit    */
    /* depth        */  8,          /* to PM's RGB structure with no    */
    /* bigEndian    */  0,          /* conversions.                     */
    /* trueColour   */  1,          /* It reduces client side load.     */
    /* redMax       */  0x0007,     /* But for reduce network traffic,  */
    /* greenMax     */  0x0007,     /* 8 bits BGR233 will be nice.      */
    /* blureMax     */  0x0003,
    /* redShift     */  0,
    /* greenShift   */  3,
    /* blueShift    */  6,
    /* pad1, pad2   */  0, 0,
    /* rectDone     */  rectDone,
    /* rectInit     */  rectInitTiny,
    /* rectDraw     */  rectDraw,
    /* rectRaw      */  rectRaw,
    /* rectCopy     */  rectCopy,
    /* rectRRE      */  rectRRE,
    /* rectCoRRE    */  rectCoRRE,
    /* rectTile     */  rectTile
} ;

/*
 * VncPix8Gray - Drawing Context for bgr233 mapped to 4 level gray scale
 */

static LONG grayMap[] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00cccccc,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00ffffff, 0x00ffffff, 0x00ffffff,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00cccccc,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00ffffff, 0x00ffffff, 0x00ffffff,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00cccccc,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,
    0x00000000, 0x00000000, 0x00000000, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00000000, 0x00000000, 0x00000000, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00808080, 0x00808080,
    0x00808080, 0x00808080, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00cccccc, 0x00cccccc, 0x00cccccc, 0x00cccccc,
    0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff,
    0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff
} ;

static  BOOL    rectInitGray(int cx, int cy)
{
    bgrMap = grayMap ;

    return rectInit(cx, cy) ;
}

VNCREC      VncCtx8Gray = {
    /* bitsPerPixel */  8,          /* Current pixel format will fit    */
    /* depth        */  8,          /* to PM's RGB structure with no    */
    /* bigEndian    */  0,          /* conversions.                     */
    /* trueColour   */  1,          /* It reduces client side load.     */
    /* redMax       */  0x0007,     /* But for reduce network traffic,  */
    /* greenMax     */  0x0007,     /* 8 bits BGR233 will be nice.      */
    /* blureMax     */  0x0003,
    /* redShift     */  0,
    /* greenShift   */  3,
    /* blueShift    */  6,
    /* pad1, pad2   */  0, 0,
    /* rectDone     */  rectDone,
    /* rectInit     */  rectInitGray,
    /* rectDraw     */  rectDraw,
    /* rectRaw      */  rectRaw,
    /* rectCopy     */  rectCopy,
    /* rectRRE      */  rectRRE,
    /* rectCoRRE    */  rectCoRRE,
    /* rectTile     */  rectTile
} ;
