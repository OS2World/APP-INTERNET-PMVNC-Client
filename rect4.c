/*
 * rect4.c - PM VNC Viewer, Pixel Handling for bgr233 8bit static color
 *           but colormap < 256, in this case maps bgr233 colors into
 *           default 16 colors or gray scale.
 *
 *
 * May 2009 - Add Dmitry Steklenev changes:
 *
 *    1. On SMP systems PMVNC thread tried to access to the hpsBitmap
 *    simultaneously. Serialized access via mutex to fix.
 *
 *    2. If "Tiny Color" and "Gray Scale" modes used, received SYS3175
 *    crash in PMMERGE.DLL. Increased malloc pbmiBitmap from *16 to *256.
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
 * ColorMap for Bitmap
 */

static  LONG   mapBmp[] = {
/*  0 CLR_BLACK     */  0x00000000,
/*  1 CLR_BLUE      */  0x000000ff,
/*  2 CLR_RED       */  0x00ff0000,
/*  3 CLR_PINK      */  0x00ff00ff,
/*  4 CLR_GREEN     */  0x0000ff00,
/*  5 CLR_CYAN      */  0x0000ffff,
/*  6 CLR_YELLOW    */  0x00ffff00,
/*  7 CLR_WHITE     */  0x00ffffff,
/*  8 CLR_DARKGRAY  */  0x00808080,
/*  9 CLR_DARKBLUE  */  0x00000080,
/* 10 CLR_DARKRED   */  0x00800000,
/* 11 CLR_DARKPINK  */  0x00800080,
/* 12 CLR_DARKGREEN */  0x00008000,
/* 13 CLR_DARKCYAN  */  0x00008080,
/* 14 CLR_BROWN     */  0x00808000,
/* 15 CLR_PALEGRAY  */  0x00cccccc
} ;

static  PUCHAR  mapRaw ;    /* convert raw bgr233 data  */
static  PLONG   mapCol ;    /* convert bgr233 color     */

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

    if (GpiCreateLogColorTable(hpsBitmap, 0, LCOLF_CONSECRGB, 0, 16, mapBmp) != TRUE) {

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

    for (i = 0 ; i < 16 ; i++) {
        *pl++ = mapBmp[i] ;
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
            *dp++ = mapRaw[*sp++] ;
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
    GpiSetColor(hpsBitmap, mapCol[color]) ;         \
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
                        *dp++ = mapRaw[*sp++] ;
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
                hextileInit(x, y, w, h, mapRaw[tileBg]) ;
                hextileDraw() ;
            } else if (subencoding & rfbHextileSubrectsColoured) {
                hextileInit(x, y, w, h, mapRaw[tileBg]) ;
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
                    hextileFill(sx, sy, sw, sh, mapRaw[sp[0]]) ;
                }
                hextileDraw() ;
            } else {
                hextileInit(x, y, w, h, mapRaw[tileBg]) ;
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
                    hextileFill(sx, sy, sw, sh, mapRaw[tileFg]) ;
                }
                hextileDraw() ;
            }
        }
    }
    DosReleaseMutexSem( hmutex ) ;
    return TRUE ;
}

/*
 * VncPix4Tiny - Drawing Context for bgr233 mapped to default 16 colors
 */

static  UCHAR   tinyRaw[] = {
     0, 10, 10, 10, 10, 10,  2,  2, 12, 11, 10, 10, 10, 10,  2,  2,
    12, 12, 14, 11, 10, 10,  2,  2, 12, 12, 12, 14, 14, 14,  2,  2,
    12, 12, 12, 14, 14, 14,  3,  3,  8,  8,  8,  8,  8, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,  4,  4,  4,  4,  4,  5,  6,  6,
     9,  9, 11, 11, 10, 10,  2,  2,  9,  9, 11, 11, 10, 10,  2,  2,
    13, 13, 11, 11, 11,  2,  2,  2, 13, 13, 13, 14, 14, 14,  2,  3,
    12, 12, 13,  8,  8,  8,  3,  3,  8,  8,  8,  8,  3, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,  4,  4,  4,  4,  5,  5,  6,  6,
     9,  9,  9, 11, 11, 11,  2,  2,  9,  9,  9, 11, 11, 11,  2,  2,
     1,  1,  1, 11, 11, 11,  3,  3, 13, 13, 13,  8,  8,  8,  3,  3,
    13, 13, 13,  8,  8,  3,  3,  3,  8,  8,  8,  8,  3, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15,  4,  4,  4,  4,  5,  5,  5,  6,  6,
     1,  1,  1,  1,  1,  8,  3,  3,  1,  1,  1,  1,  1,  8,  3,  3,
     1,  1,  1,  1,  8,  8,  3,  3,  1,  1,  1,  8,  8,  3,  3,  3,
    13, 13,  8,  8,  3,  3,  3, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15,  5,  5,  5,  5,  5,  5,  5,  6,  6,  7
} ;

static  LONG    tinyCol[] = {
    CLR_BLACK,      CLR_DARKRED,    CLR_DARKRED,    CLR_DARKRED,
    CLR_DARKRED,    CLR_DARKRED,    CLR_RED,        CLR_RED,
    CLR_DARKGREEN,  CLR_DARKPINK,   CLR_DARKRED,    CLR_DARKRED,
    CLR_DARKRED,    CLR_DARKRED,    CLR_RED,        CLR_RED,
    CLR_DARKGREEN,  CLR_DARKGREEN,  CLR_BROWN,      CLR_DARKPINK,
    CLR_DARKRED,    CLR_DARKRED,    CLR_RED,        CLR_RED,
    CLR_DARKGREEN,  CLR_DARKGREEN,  CLR_DARKGREEN,  CLR_BROWN,
    CLR_BROWN,      CLR_BROWN,      CLR_RED,        CLR_RED,
    CLR_DARKGREEN,  CLR_DARKGREEN,  CLR_DARKGREEN,  CLR_BROWN,
    CLR_BROWN,      CLR_BROWN,      CLR_PINK,       CLR_PINK,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_GREEN,      CLR_GREEN,      CLR_GREEN,      CLR_GREEN,
    CLR_GREEN,      CLR_CYAN,       CLR_YELLOW,     CLR_YELLOW,
    CLR_DARKBLUE,   CLR_DARKBLUE,   CLR_DARKPINK,   CLR_DARKPINK,
    CLR_DARKRED,    CLR_DARKRED,    CLR_RED,        CLR_RED,
    CLR_DARKBLUE,   CLR_DARKBLUE,   CLR_DARKPINK,   CLR_DARKPINK,
    CLR_DARKRED,    CLR_DARKRED,    CLR_RED,        CLR_RED,
    CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKPINK,   CLR_DARKPINK,
    CLR_DARKPINK,   CLR_RED,        CLR_RED,        CLR_RED,
    CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_BROWN,
    CLR_BROWN,      CLR_BROWN,      CLR_RED,        CLR_PINK,
    CLR_DARKGREEN,  CLR_DARKGREEN,  CLR_DARKCYAN,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_PINK,       CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_GREEN,      CLR_GREEN,      CLR_GREEN,      CLR_GREEN,
    CLR_CYAN,       CLR_CYAN,       CLR_YELLOW,     CLR_YELLOW,
    CLR_DARKBLUE,   CLR_DARKBLUE,   CLR_DARKBLUE,   CLR_DARKPINK,
    CLR_DARKPINK,   CLR_DARKPINK,   CLR_RED,        CLR_RED,
    CLR_DARKBLUE,   CLR_DARKBLUE,   CLR_DARKBLUE,   CLR_DARKPINK,
    CLR_DARKPINK,   CLR_DARKPINK,   CLR_RED,        CLR_RED,
    CLR_BLUE,       CLR_BLUE,       CLR_BLUE,       CLR_DARKPINK,
    CLR_DARKPINK,   CLR_DARKPINK,   CLR_PINK,       CLR_PINK,
    CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,
    CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,       CLR_PINK,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_PINK,       CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_GREEN,
    CLR_GREEN,      CLR_GREEN,      CLR_GREEN,      CLR_CYAN,
    CLR_CYAN,       CLR_CYAN,       CLR_YELLOW,     CLR_YELLOW,
    CLR_BLUE,       CLR_BLUE,       CLR_BLUE,       CLR_BLUE,
    CLR_BLUE,       CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,
    CLR_BLUE,       CLR_BLUE,       CLR_BLUE,       CLR_BLUE,
    CLR_BLUE,       CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,
    CLR_BLUE,       CLR_BLUE,       CLR_BLUE,       CLR_BLUE,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,
    CLR_BLUE,       CLR_BLUE,       CLR_BLUE,       CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_PINK,       CLR_PINK,       CLR_PINK,
    CLR_DARKCYAN,   CLR_DARKCYAN,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_PINK,       CLR_PINK,       CLR_PINK,       CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_CYAN,       CLR_CYAN,
    CLR_CYAN,       CLR_CYAN,       CLR_CYAN,       CLR_CYAN,
    CLR_CYAN,       CLR_YELLOW,     CLR_YELLOW,     CLR_WHITE
} ;

static  BOOL    rectInitTiny(int cx, int cy)
{
    mapRaw = tinyRaw ;
    mapCol = tinyCol ;

    return rectInit(cx, cy) ;
}

VNCREC      VncCtx4Tiny = {
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

static  UCHAR   grayRaw[] = {
     0,  0,  0,  0,  0,  8,  8,  8,  0,  0,  0,  0,  0,  8,  8,  8,
     0,  0,  0,  0,  0,  8,  8,  8,  0,  0,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8, 15,  8,  8,  8,  8,  8, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  7,  7,  7,
     0,  0,  0,  0,  0,  8,  8,  8,  0,  0,  0,  0,  0,  8,  8,  8,
     0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8, 15,  8,  8,  8,  8,  8, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  7,  7,  7,
     0,  0,  0,  0,  0,  8,  8,  8,  0,  0,  0,  0,  0,  8,  8,  8,
     0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8, 15,  8,  8,  8,  8, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  7,  7,  7,  7,
     0,  0,  0,  8,  8,  8,  8,  8,  0,  0,  0,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,  7,  7,  7,  7,  7,  7,  7,  7
} ;

static  LONG    grayCol[] = {
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PALEGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_WHITE,      CLR_WHITE,      CLR_WHITE,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PALEGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_WHITE,      CLR_WHITE,      CLR_WHITE,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_BLACK,      CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_BLACK,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PALEGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_WHITE,      CLR_WHITE,      CLR_WHITE,      CLR_WHITE,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_BLACK,      CLR_BLACK,      CLR_BLACK,      CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_DARKGRAY,
    CLR_DARKGRAY,   CLR_DARKGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,   CLR_PALEGRAY,
    CLR_WHITE,      CLR_WHITE,      CLR_WHITE,      CLR_WHITE,
    CLR_WHITE,      CLR_WHITE,      CLR_WHITE,      CLR_WHITE
} ;

static  BOOL    rectInitGray(int cx, int cy)
{
    mapRaw = grayRaw ;
    mapCol = grayCol ;

    return rectInit(cx, cy) ;
}

VNCREC      VncCtx4Gray = {
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
