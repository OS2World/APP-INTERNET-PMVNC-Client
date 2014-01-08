/*
 * rect32.c - PM VNC Viewer, Pixel Handling for 32 bit true color
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

static  BITMAPINFO2     bmiBitmap ;

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

    if (DosCreateMutexSem( NULL, &hmutex, 0, 0 ) != 0) {
        rectDone() ;
        return FALSE ;
    }

    /*
     * prepare buffer for drawing
     */

    rbuff = malloc(RBUFSIZ) ;
    ibuff = malloc(MAXSCAN) ;

    if (rbuff == NULL || ibuff == NULL) {
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
    bmi.cBitCount     = 24 ;
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

    if (GpiCreateLogColorTable(hpsBitmap, 0, LCOLF_RGB, 0, 0, NULL) != TRUE) {

#ifdef DEBUG
        TRACE("rectInit - failed to set color mode RGB\n") ;
#endif

        rectDone() ;
        return FALSE ;
    }

    /*
     * put initial message on it
     */

    pt.x = 32 ;
    pt.y = cyBitmap - 64 ;
    GpiErase(hpsBitmap) ;
    GpiCharStringAt(hpsBitmap, &pt, strlen(VncGreeting), VncGreeting) ;

    /*
     * prepare bitmap info. header for later operation
     */

    memset(&bmiBitmap, 0, sizeof(BITMAPINFO2)) ;
    bmiBitmap.cbFix     = 16 ;
    bmiBitmap.cPlanes   =  1 ;
    bmiBitmap.cBitCount = 24 ;

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

    bytesPerLine = ((r->w * 3) + 3) & ~0x03 ;

    for (i = 0, sp = buff ; i < lines ; i++) {
        dp = &ibuff[bytesPerLine * (lines - i - 1)] ;
        for (j = 0 ; j < r->w ; j++) {
            dp[0] = sp[0] ;     /* Blue     */
            dp[1] = sp[1] ;     /* Green    */
            dp[2] = sp[2] ;     /* Red      */
            sp += 4 ;
            dp += 3 ;
        }
    }

    bmiBitmap.cx = r->w  ;
    bmiBitmap.cy = lines ;

    apt[0].x = r->x ;
    apt[0].y = (cyBitmap - r->y - lines) ;
    apt[1].x = apt[0].x + r->w  - 1 ;
    apt[1].y = apt[0].y + lines - 1 ;
    apt[2].x = 0 ;
    apt[2].y = 0 ;
    apt[3].x = r->w  ;
    apt[3].y = lines ;

    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ;
    GpiDrawBits(hpsBitmap, ibuff, &bmiBitmap, 4, apt, ROP_SRCCOPY, 0) ;
    DosReleaseMutexSem( hmutex ) ;
}

static  BOOL    rectRaw(rfbRectangle *r)
{
    int     bytesPerLine, linesToRead ;

    bytesPerLine  = r->w * 4 ;      /* for 32 bits depth specific   */
    linesToRead   = MINBUF / bytesPerLine ;

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
    POINTL  apt[4] ;                                \
    DosRequestMutexSem( hmutex, SEM_INDEFINITE_WAIT ) ; \
    GpiSetColor(hpsBitmap, (LONG) (color)) ;        \
    if ((mw) == 1 && (mh) == 1) {                   \
        apt[0].x = (mx)                   ;         \
        apt[0].y = cyBitmap - (my) - (mh) ;         \
        GpiSetPel(hpsBitmap, &apt[0])     ;         \
    } else if ((mw) == 1) {                         \
        apt[0].x = (mx)                   ;         \
        apt[0].y = cyBitmap - (my) - (mh) ;         \
        apt[1].x = (mx)                   ;         \
        apt[1].y = cyBitmap - (my) - 1    ;         \
        GpiMove(hpsBitmap, &apt[0])       ;         \
        GpiLine(hpsBitmap, &apt[1])       ;         \
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
    CARD32  color    ;
    int     i        ;

    if (netRecv((PUCHAR) &hdr, sz_rfbRREHeader) != TRUE) {
        netFail("failed to recv RREHeader") ;
        return FALSE ;
    }
    hdr.nSubrects = swap32(hdr.nSubrects) ;

    if (netRecv((PUCHAR) &color, sizeof(CARD32)) != TRUE) {
        netFail("failed to recv BG color") ;
        return FALSE ;
    }

    color &= 0x00ffffff ;
    fillRect(r->x, r->y, r->w, r->h, color) ;

    for (i = 0 ; i < hdr.nSubrects ; i++) {
        if (netRecv((PUCHAR) &color, sizeof(CARD32)) != TRUE) {
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

        color &= 0x00ffffff ;
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
    CARD32  color    ;
    int     i        ;

    if (netRecv((PUCHAR) &hdr, sz_rfbRREHeader) != TRUE) {
        netFail("failed to recv RREHeader") ;
        return FALSE ;
    }
    hdr.nSubrects = swap32(hdr.nSubrects) ;

    if (netRecv((PUCHAR) &color, sizeof(CARD32)) != TRUE) {
        netFail("failed to recv BG color") ;
        return FALSE ;
    }

    color &= 0x00ffffff ;
    fillRect(r->x, r->y, r->w, r->h, color) ;

    if (netRecv(rbuff, hdr.nSubrects * 8) != TRUE) {
        netFail("failed to recv subrects") ;
        return FALSE ;
    }

    for (i = 0, sp = rbuff ; i < hdr.nSubrects ; i++, sp += 8) {
        color = *(CARD32 *) sp ;
        subrect.x = r->x + sp[4] ;
        subrect.y = r->y + sp[5] ;
        subrect.w = sp[6] ;
        subrect.h = sp[7] ;

        color &= 0x00ffffff ;
        fillRect(subrect.x, subrect.y, subrect.w, subrect.h, color) ;
    }
    return TRUE ;
}

/*
 * rectTile - Hextile encoding
 */

static  CARD32  tileFg = 0xffffffff ;
static  CARD32  tileBg = 0x00000000 ;

static  PUCHAR  tileScan[16] ;
static  POINTL  tileBlit[4]  ;

static  void    hextileInit(int x, int y, int w, int h, CARD32 bg)
{
    int     i, j, bytesPerLine ;
    PUCHAR  p, cp ;

    /*
     * fill tile with background color
     */

    bytesPerLine = ((w * 3) + 3) & ~0x03 ;
    cp = (PUCHAR) &bg ;

    for (i = 0 ; i < h  ; i++) {
        tileScan[i] = p = &ibuff[bytesPerLine * (h - i - 1)] ;
        for (j = 0 ; j < w ; j++) {
            p[0] = cp[0] ;
            p[1] = cp[1] ;
            p[2] = cp[2] ;
            p += 3 ;
        }
    }

    /*
     * prepare for final bitblt
     */

    bmiBitmap.cx = w ;
    bmiBitmap.cy = h ;

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
    GpiDrawBits(hpsBitmap, ibuff, &bmiBitmap, 4, tileBlit, ROP_SRCCOPY, 0)

#define hextileFill(x, y, w, h, color)      \
{                                           \
    int     i, j ;                          \
    ULONG   lcolor = (color) ;              \
    PUCHAR  p, cp ;                         \
    cp = (PUCHAR) &lcolor ;                 \
    for (i = 0 ; i < (h) ; i++) {           \
        p = tileScan[(y) + i] + ((x) * 3) ; \
        for (j = 0 ; j < (w) ; j++) {       \
            p[0] = cp[0] ;                  \
            p[1] = cp[1] ;                  \
            p[2] = cp[2] ;                  \
            p += 3 ;                        \
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
    PULONG  cp     ;
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

                bytesPerLine = ((w * 3) + 3) & ~0x03 ;

                if (netRecv(rbuff, w * h * 4) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to hextile raw") ;
                    return FALSE ;
                }

                for (i = 0, sp = rbuff ; i < h ; i++) {
                    dp = &ibuff[bytesPerLine * (h - i - 1)] ;
                    for (j = 0 ; j < w ; j++) {
                        dp[0] = sp[0] ;
                        dp[1] = sp[1] ;
                        dp[2] = sp[2] ;
                        sp += 4 ;
                        dp += 3 ;
                    }
                }

                bmiBitmap.cx = w ;
                bmiBitmap.cy = h ;

                apt[0].x = x ;
                apt[0].y = (cyBitmap - y - h) ;
                apt[1].x = apt[0].x + w  - 1 ;
                apt[1].y = apt[0].y + h - 1 ;
                apt[2].x = 0 ;
                apt[2].y = 0 ;
                apt[3].x = w ;
                apt[3].y = h ;

                GpiDrawBits(hpsBitmap, ibuff,
                                &bmiBitmap, 4, apt, ROP_SRCCOPY, 0) ;
                continue ;
            }
            if (subencoding & rfbHextileBackgroundSpecified) {
                if (netRecv((PUCHAR) &tileBg, sizeof(CARD32)) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv BG color") ;
                    return FALSE ;
                }
                tileBg &= 0x00ffffff ;
            }
            if (subencoding & rfbHextileForegroundSpecified) {
                if (netRecv((PUCHAR) &tileFg, sizeof(CARD32)) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv FG color") ;
                    return FALSE ;
                }
                tileFg &= 0x00ffffff ;
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
                if (netRecv((PUCHAR) rbuff, nSubrects * 6) != TRUE) {
                    DosReleaseMutexSem( hmutex ) ;
                    netFail("failed to recv hextile Subrects") ;
                    return FALSE ;
                }

                for (sn = 0, sp = rbuff ; sn < nSubrects ; sn++, sp += 6) {
                    cp = (PULONG) sp ;
                    sx = rfbHextileExtractX(sp[4]) ;
                    sy = rfbHextileExtractY(sp[4]) ;
                    sw = rfbHextileExtractW(sp[5]) ;
                    sh = rfbHextileExtractH(sp[5]) ;
                    hextileFill(sx, sy, sw, sh, *cp) ;
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
 * VncPix32 - Drawing Context for 32 bit true color
 */

VNCREC      VncCtx32 = {
    /* bitsPerPixel */  32,         /* Current pixel format will fit    */
    /* depth        */  32,         /* to PM's RGB structure with no    */
    /* bigEndian    */  0,          /* conversions.                     */
    /* trueColour   */  1,          /* It reduces client side load.     */
    /* redMax       */  0x00ff,     /* But for reduce network traffic,  */
    /* greenMax     */  0x00ff,     /* 8 bits BGR233 will be nice.      */
    /* blureMax     */  0x00ff,
    /* redShift     */  16,
    /* greenShift   */  8,
    /* blueShift    */  0,
    /* pad1, pad2   */  0, 0,
    /* rectDone     */  rectDone,
    /* rectInit     */  rectInit,
    /* rectDraw     */  rectDraw,
    /* rectRaw      */  rectRaw,
    /* rectCopy     */  rectCopy,
    /* rectRRE      */  rectRRE,
    /* rectCoRRE    */  rectCoRRE,
    /* rectTile     */  rectTile
} ;
