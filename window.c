/*
 * window.c - PM VNC Viewer, Window Manipulations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "pmvncres.h"

extern void keyFocusChange(HWND hwnd);

/*
 * window handles and their size & position
 */

HWND    hwndFrame  = NULLHANDLE ;
HWND    hwndClient = NULLHANDLE ;
HWND    hwndHorz   = NULLHANDLE ;
HWND    hwndVert   = NULLHANDLE ;

static  SWP     swpFrame  = { 0 } ; /* size/pos of frame  window    */
static  SWP     swpClient = { 0 } ; /* size/pos of client window    */
static  SIZEL   sizMax    = { 0 } ; /* max size of frame            */
static  SIZEL   sizRfb    = { 0 } ; /* size of remote screen        */
static  POINTL  mapRfb    = { 0 } ; /* mapping position             */

static  ULONG   sv_cxScreen     ;   /* width  of screen             */
static  ULONG   sv_cyScreen     ;   /* height of screen             */
static  ULONG   sv_cyTitleBar   ;   /* height of title bar          */
static  ULONG   sv_cxSizeBorder ;   /* width  of sizing border      */
static  ULONG   sv_cySizeBorder ;   /* height of sizing border      */

/*
 * winIconized - TRUE if window was iconized
 */

static  BOOL    isIconized = FALSE ;

BOOL    winIconized(void)
{
    return isIconized ;
}

/*
 * procFrame - subclassed frame window
 */

static  PFNWP   pfnFrame ;

static MRESULT EXPENTRY procFrame(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    PSWP    pswp ;

    switch (msg) {

    case WM_PAINT :
        kstAdjust(hwndFrame) ;
        break ;

    case WM_WINDOWPOSCHANGED :
        pswp = (PSWP) PVOIDFROMMP(mp1) ;
        swpFrame = *pswp ;
        if (! isIconized && swpFrame.cx > 0 && swpFrame.cy > 0) {
            SessSavedPos.x  = swpFrame.x  ;
            SessSavedPos.y  = swpFrame.y  ;
            SessSavedSiz.cx = swpFrame.cx ;
            SessSavedSiz.cy = swpFrame.cy ;
        }
        break ;

    case WM_ADJUSTWINDOWPOS :
        pswp = (PSWP) PVOIDFROMMP(mp1) ;
        if ((pswp->fl & SWP_MAXIMIZE) && (pswp->fl & SWP_RESTORE) == 0) {
            pswp->fl &= ~SWP_MAXIMIZE ;
            pswp->fl |=  (SWP_SIZE | SWP_MOVE) ;
            pswp->cx = sizMax.cx ;
            pswp->cy = sizMax.cy ;
            pswp->y -= (sizMax.cy - swpFrame.cy) ;

            if (pswp->cx > sv_cxScreen) {
                pswp->x = -(sv_cxSizeBorder) ;
            } else if (pswp->x < 0) {
                pswp->x = 0 ;
            } else if ((pswp->x + pswp->cx) > sv_cxScreen) {
                pswp->x -= (pswp->x + pswp->cx - sv_cxScreen) ;
            }
            if (pswp->cy > sv_cyScreen) {
                pswp->y = -(pswp->cy - sv_cyScreen - sv_cyTitleBar - sv_cySizeBorder) ;
            } else if (pswp->y < 0) {
                pswp->y = 0 ;
            } else if ((pswp->y + pswp->cy) > sv_cyScreen) {
                pswp->y -= (pswp->y + pswp->cy - sv_cyScreen) ;
            }
        }
        if (pswp->fl & SWP_MINIMIZE) {
            isIconized = TRUE ;
        }
        if (pswp->fl & SWP_RESTORE) {
            isIconized = FALSE ;
            protoSendRequest(TRUE, NULL) ;
        }

        /*
         * limit window sizing
         */

        if ((pswp->fl & SWP_SIZE) && (pswp->cx > sizMax.cx)) {
            if (pswp->x == swpFrame.x) {    /* Enlarged to Right */
                pswp->x += (pswp->cx - sizMax.cx) ;
                pswp->fl |= SWP_MOVE ;
            }
            pswp->cx = sizMax.cx ;
        }
        if ((pswp->fl & SWP_SIZE) && (pswp->cy > sizMax.cy)) {
            if (pswp->y == swpFrame.y) {    /* Enlaged to Upper */
                pswp->y += (pswp->cy - sizMax.cy) ;
                pswp->fl |= SWP_MOVE ;
            }
            pswp->cy = sizMax.cy ;
        }

        /*
         * control scroll bars
         */

        if (hwndHorz != NULLHANDLE && (pswp->fl & SWP_SIZE)) {
            if (pswp->cx >= sizMax.cx) {
                WinSetParent(hwndHorz, HWND_OBJECT, FALSE) ;
            } else {
                WinSetParent(hwndHorz, hwnd, FALSE) ;
            }
        }
        if (hwndVert != NULLHANDLE && (pswp->fl & SWP_SIZE)) {
            if (pswp->cy >= sizMax.cy) {
                WinSetParent(hwndVert, HWND_OBJECT, FALSE) ;
            } else {
                WinSetParent(hwndVert, hwnd, FALSE) ;
            }
        }
        break ;

    case WM_INITMENU :
        menuHook(hwnd, SHORT1FROMMP(mp1)) ;
        return (MRESULT) 0 ;
    }
    return (*pfnFrame) (hwnd, msg, mp1, mp2) ;
}

/*
 * winSetup - adjust window with Remote Frame Buffer
 */

static  BOOL    dontAdjustY = FALSE ;

static  BOOL    winSetup(SHORT cx, SHORT cy, PUCHAR str)
{
    UCHAR   title[256] ;

    if (hwndFrame == NULLHANDLE || hwndClient == NULLHANDLE) {
        errMessage("winSetup - no windows") ;
        return FALSE ;
    }

    int titleSize = strlen(ProgramVers) + 4;

    // can remember - need to do max "str" length check before
    // it crashes someone's system :)
    if ((str == NULL) || (strlen(str) > titleSize)) strcpy(title, ProgramVers);
    else sprintf(title, "%s [%s]", ProgramVers, str);

    /*
     * Save & Initialize RFB size and position
     */

    sizRfb.cx = cx ;
    sizRfb.cy = cy ;
    mapRfb.x  = 0  ;
    mapRfb.y  = sizRfb.cy ;

    sizMax.cx = sizRfb.cx + (sv_cxSizeBorder * 2) - 1 ;
    sizMax.cy = sizRfb.cy + (sv_cySizeBorder * 2) + sv_cyTitleBar - 1 ;

    if (SessSavedRfb.cx == sizRfb.cx && SessSavedRfb.cy == sizRfb.cy) {
        /*
         * RFB size is save as last session, then use last positions
         */
        swpFrame.x  = SessSavedPos.x  ;
        swpFrame.y  = SessSavedPos.y  ;
        swpFrame.cx = SessSavedSiz.cx ;
        swpFrame.cy = SessSavedSiz.cy ;
        mapRfb.x    = SessSavedOff.x  ;
        mapRfb.y    = SessSavedOff.y  ;
        dontAdjustY = TRUE ;

#ifdef DEBUG
        TRACE("winSetup - use last positions (off %d,%d)\n",
                                            mapRfb.x, mapRfb.y) ;
#endif

    } else {

        /*
         * otherwize, center window and place RFB at top-left
         */
        if (sizMax.cx <= sv_cxScreen) {
            swpFrame.cx = sizMax.cx ;
            swpFrame.x = (sv_cxScreen - sizMax.cx) / 2 ;
        } else {
            swpFrame.cx = sv_cxScreen ;
            swpFrame.x  = 0 ;
        }
        if (sizMax.cy <= sv_cyScreen) {
            swpFrame.cy = sizMax.cy ;
            swpFrame.y = (sv_cyScreen - sizMax.cy) / 2 ;
        } else {
            swpFrame.cy = sv_cyScreen ;
            swpFrame.y = 0 ;
        }

#ifdef DEBUG
        TRACE("winSetup - use new  positions (off %d,%d)\n",
                                            mapRfb.x, mapRfb.y) ;
#endif

    }

    /*
     * show window
     */

    WinSetWindowText(hwndFrame, title) ;
    WinSetWindowPos(hwndFrame, NULLHANDLE,
        swpFrame.x, swpFrame.y, swpFrame.cx, swpFrame.cy,
        (SWP_MOVE | SWP_SIZE | SWP_SHOW)) ;

    SessSavedRfb = sizRfb ;

    return TRUE ;
}

/*
 * mapSize - adjust RFB position to client window size
 */

static  void    mapSize(SHORT cx, SHORT cy)
{
    SHORT   diff, vpos ;

    if (dontAdjustY) {
        dontAdjustY = FALSE ;
    } else if ((mapRfb.y += (swpClient.cy - cy)) < 0) {
        mapRfb.y = 0 ;
    }

    swpClient.cx = cx ;
    swpClient.cy = cy ;

    if ((mapRfb.x + swpClient.cx) >= sizRfb.cx) {
        mapRfb.x = sizRfb.cx - swpClient.cx ;
    }
    if ((mapRfb.y + swpClient.cy) >= sizRfb.cy) {
        mapRfb.y = sizRfb.cy - swpClient.cy ;
    }

    if (swpClient.cx >= sizRfb.cx) {
        WinEnableWindow(hwndHorz, FALSE) ;
    } else {
        diff = sizRfb.cx - swpClient.cx ;
        WinSendMsg(hwndHorz, SBM_SETTHUMBSIZE,
                MPFROM2SHORT(swpClient.cx, sizRfb.cx), NULL) ;
        WinSendMsg(hwndHorz, SBM_SETSCROLLBAR,
                MPFROM2SHORT(mapRfb.x, 0), MPFROM2SHORT(0, diff)) ;
        WinEnableWindow(hwndHorz, TRUE) ;
    }

    if (swpClient.cy >= sizRfb.cy) {
        WinEnableWindow(hwndVert, FALSE) ;
    } else {
        diff = sizRfb.cy - swpClient.cy ;
        vpos = diff - mapRfb.y ;
        WinSendMsg(hwndVert, SBM_SETTHUMBSIZE,
                MPFROM2SHORT(swpClient.cy, sizRfb.cy), NULL) ;
        WinSendMsg(hwndVert, SBM_SETSCROLLBAR,
                MPFROM2SHORT(vpos, 0), MPFROM2SHORT(0, diff)) ;
        WinEnableWindow(hwndVert, TRUE) ;
    }

    if (! isIconized) {
        SessSavedPos.x  = swpFrame.x  ;
        SessSavedPos.y  = swpFrame.y  ;
        SessSavedSiz.cx = swpFrame.cx ;
        SessSavedSiz.cy = swpFrame.cy ;
        SessSavedOff.x  = mapRfb.x    ;
        SessSavedOff.y  = mapRfb.y    ;
    }

#ifdef DEBUG
    TRACE("mapSize off %d %d\n", mapRfb.x, mapRfb.y) ;
#endif
}

/*
 * mapHorz - adjust horizontal mapping
 */

#define PAGEDIV     8
#define LINEUNIT    8

static  void    mapHorz(USHORT cmd, SHORT x)
{
    SHORT       new, lim ;
    BOOL        set ;

    lim = sizRfb.cx - swpClient.cx ;

    switch (cmd) {
    case SB_SLIDERTRACK    :
    case SB_SLIDERPOSITION :
        new = x     ;
        set = FALSE ;
        break ;
    case SB_LINELEFT :
        new = mapRfb.x - LINEUNIT ;
        set = TRUE ;
        break ;
    case SB_LINERIGHT :
        new = mapRfb.x + LINEUNIT ;
        set = TRUE ;
        break ;
    case SB_PAGELEFT :
        new = mapRfb.x - (sizRfb.cx / PAGEDIV) ;
        set = TRUE ;
        break ;
    case SB_PAGERIGHT :
        new = mapRfb.x + (sizRfb.cx / PAGEDIV) ;
        set = TRUE ;
        break ;
    default :
        return ;
    }

    if (new < 0)   new = 0   ;
    if (new > lim) new = lim ;

    if (set) {
        WinSendMsg(hwndHorz, SBM_SETSCROLLBAR,
                MPFROM2SHORT(new, 0), MPFROM2SHORT(0, lim)) ;
    }
    if (new != mapRfb.x) {
        mapRfb.x = new ;
        WinInvalidateRect(hwndClient, NULL, FALSE) ;
    }
    if (! isIconized) {
        SessSavedOff.x  = mapRfb.x ;
        SessSavedOff.y  = mapRfb.y ;
    }

#ifdef DEBUG
    TRACE("mapHorz off %d %d\n", mapRfb.x, mapRfb.y) ;
#endif
}

/*
 * mapVert - adjust vertical mapping
 */

static  void    mapVert(USHORT cmd, SHORT y)
{
    SHORT       new, lim ;
    BOOL        set ;

    lim = sizRfb.cy - swpClient.cy ;

    switch (cmd) {
    case SB_SLIDERTRACK    :
    case SB_SLIDERPOSITION :
        new = lim - y ;
        set = FALSE ;
        break ;
    case SB_LINEUP :
        new = mapRfb.y + LINEUNIT ;
        set = TRUE ;
        break ;
    case SB_LINEDOWN :
        new = mapRfb.y - LINEUNIT ;
        set = TRUE ;
        break ;
    case SB_PAGEUP :
        new = mapRfb.y + (sizRfb.cy / PAGEDIV) ;
        set = TRUE ;
        break ;
    case SB_PAGEDOWN :
        new = mapRfb.y - (sizRfb.cy / PAGEDIV) ;
        set = TRUE ;
        break ;
    default :
        return ;
    }

    if (new < 0)   new = 0   ;
    if (new > lim) new = lim ;

    if (set) {
        WinSendMsg(hwndVert, SBM_SETSCROLLBAR,
                MPFROM2SHORT((lim - new), 0), MPFROM2SHORT(0, lim)) ;
    }
    if (new != mapRfb.y) {
        mapRfb.y = new ;
        WinInvalidateRect(hwndClient, NULL, FALSE) ;
    }
    if (! isIconized) {
        SessSavedOff.x  = mapRfb.x ;
        SessSavedOff.y  = mapRfb.y ;
    }

#ifdef DEBUG
    TRACE("mapVert off %d %d\n", mapRfb.x, mapRfb.y) ;
#endif
}

/*
 * mapPoint - map window point to RFB point
 */

static  void    mapPoint(SHORT x, SHORT y, PPOINTL pt)
{
    /*
     * don't exceed current window
     */
    if (x < 0) x = 0 ;
    if (x > swpClient.cx) x = swpClient.cx ;
    if (y < 0) y = 0 ;
    if (y > swpClient.cy) y = swpClient.cy ;

    /*
     * convert to Remote Frame Buffer position
     */
    x += mapRfb.x ;
    y += mapRfb.y ;
    pt->x = x             ;
    pt->y = sizRfb.cy - y ;
}

/*
 * procClient - client window procedure
 */

static  BOOL    mouseCaptured = FALSE ;

static MRESULT EXPENTRY procClient(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    HPS     hps ;
    RECTL   rct ;
    POINTL  pt  ;
    POINTL  apt[4] ;
    SHORT   x, y, w, h ;

    switch (msg) {

    case WM_CREATE :
        mouseInit(hwnd) ;
        return (MRESULT) 0 ;
    case WM_DESTROY :
        mouseDone(hwnd) ;
        return (MRESULT) 0 ;

    case WM_SIZE  :
        mapSize(SHORT1FROMMP(mp2), SHORT2FROMMP(mp2)) ;
        return (MRESULT) 0 ;

    case WM_HSCROLL :
        mapHorz(SHORT2FROMMP(mp2), SHORT1FROMMP(mp2)) ;
        return (MRESULT) 0 ;
    case WM_VSCROLL :
        mapVert(SHORT2FROMMP(mp2), SHORT1FROMMP(mp2)) ;
        return (MRESULT) 0 ;

    case WM_MOUSEMOVE     :
        mapPoint(SHORT1FROMMP(mp1), SHORT2FROMMP(mp1), &pt) ;
        mouseMove(hwnd, &pt) ;
        return (MRESULT) 0 ;

    case WM_BUTTON1DOWN   :
    case WM_BUTTON2DOWN   :
    case WM_BUTTON3DOWN   :
        if (mouseCaptured == FALSE) {
            WinSetCapture(HWND_DESKTOP, hwnd) ;
            mouseCaptured = TRUE ;
        }
        mapPoint(SHORT1FROMMP(mp1), SHORT2FROMMP(mp1), &pt) ;
        mouseEvent(hwnd, msg, &pt) ;
        return WinDefWindowProc(hwnd, msg, mp1, mp2) ;

    case WM_BUTTON1UP     :
    case WM_BUTTON2UP     :
    case WM_BUTTON3UP     :
        if (mouseCaptured == TRUE) {
            WinSetCapture(HWND_DESKTOP, NULLHANDLE) ;
            mouseCaptured = FALSE ;
        }
        mapPoint(SHORT1FROMMP(mp1), SHORT2FROMMP(mp1), &pt) ;
        mouseEvent(hwnd, msg, &pt) ;
        return WinDefWindowProc(hwnd, msg, mp1, mp2) ;

    case WM_BUTTON1CLICK  :
    case WM_BUTTON2CLICK  :
    case WM_BUTTON3CLICK  :
        if (mouseCaptured == TRUE) {
            WinSetCapture(HWND_DESKTOP, NULLHANDLE) ;
            mouseCaptured = FALSE ;
        }
        mapPoint(SHORT1FROMMP(mp1), SHORT2FROMMP(mp1), &pt) ;
        mouseEvent(hwnd, msg, &pt) ;
        return WinDefWindowProc(hwnd, msg, mp1, mp2) ;

    case WM_BUTTON1DBLCLK :
    case WM_BUTTON2DBLCLK :
    case WM_BUTTON3DBLCLK :
        if (mouseCaptured == FALSE) {
            WinSetCapture(HWND_DESKTOP, hwnd) ;
            mouseCaptured = TRUE ;
        }
        mapPoint(SHORT1FROMMP(mp1), SHORT2FROMMP(mp1), &pt) ;
        mouseEvent(hwnd, msg, &pt) ;
        return WinDefWindowProc(hwnd, msg, mp1, mp2) ;

    case WM_CHAR :
        keyEvent(hwnd,
            SHORT1FROMMP(mp1), SHORT1FROMMP(mp2),
            SHORT2FROMMP(mp2), (USHORT) CHAR4FROMMP(mp1)) ;
        return (MRESULT) 0 ;

    case WM_SETFOCUS :
        keyFocusChange(hwnd) ;
        return (MRESULT) 0 ;

    case WM_TIMER :
        mouseTimer(hwnd) ;
        return (MRESULT) 0 ;

    /*
     * local messages
     */

    case WM_VNC_FAIL :
        errMessage((PUCHAR) mp2) ;
        WinPostMsg(hwnd, WM_CLOSE, NULL, NULL) ;
        return (MRESULT) 0 ;

    case WM_VNC_AUTH :
        authCypher(hwnd, (PUCHAR) mp2) ;
        protoSendAuth() ;
        return (MRESULT) 0 ;

    case WM_VNC_INIT :
        if (winSetup(SHORT1FROMMP(mp1), SHORT2FROMMP(mp1),
                                                (PUCHAR) mp2) != TRUE) {
            WinPostMsg(hwnd, WM_CLOSE, NULL, NULL) ;
            return (MRESULT) 0 ;
        }
        return (MRESULT) 0 ;

    case WM_VNC_BELL :
        if (SessOptDeiconify) {
            WinSetWindowPos(hwndFrame,
                    HWND_TOP, 0, 0, 0, 0, (SWP_RESTORE | SWP_ZORDER)) ;
        }
        WinAlarm(HWND_DESKTOP, WA_NOTE) ;
        return (MRESULT) 0 ;

    case WM_VNC_CLIP :
        clipHold(hwnd, (PUCHAR) mp2) ;
        return (MRESULT) 0 ;

    case WM_VNC_UPDATE :
        x = SHORT1FROMMP(mp1) ;
        y = SHORT2FROMMP(mp1) ;
        w = SHORT1FROMMP(mp2) ;
        h = SHORT2FROMMP(mp2) ;
        rct.xLeft   = x     - mapRfb.x - 1 ;
        rct.xRight  = x + w - mapRfb.x + 1 ;
        rct.yBottom = y     - mapRfb.y - 1 ;
        rct.yTop    = y + h - mapRfb.y + 1 ;
        WinInvalidateRect(hwndClient, &rct, FALSE) ;
        return (MRESULT) 0 ;

    case WM_PAINT :
        hps = WinBeginPaint(hwnd, NULLHANDLE, &rct) ;
        apt[0].x = rct.xLeft   ;
        apt[0].y = rct.yBottom ;
        apt[1].x = rct.xRight  ;
        apt[1].y = rct.yTop    ;
        apt[2].x = mapRfb.x + rct.xLeft   ;
        apt[2].y = mapRfb.y + rct.yBottom ;
        apt[3].x = mapRfb.x + rct.xRight  ;
        apt[3].y = mapRfb.y + rct.yTop    ;
        (*VncCtx->rectDraw) (hps, apt) ;
        WinEndPaint(hps) ;
        return (MRESULT) 0 ;

    case WM_COMMAND :
        menuProc(hwnd, SHORT1FROMMP(mp1)) ;
        return (MRESULT) 0 ;
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2) ;
}

/*
 * winCreate - create windows
 *      windows are first invisible and 0 sized.  They are sized to
 *      RFB on received ServerInit message from VNC server.
 */

#define VNCPROC "vncViewer"

BOOL    winCreate(HAB hab)
{
    ULONG   flFrameStyle ;
    ULONG   flFrameFlags ;

    if (hwndFrame != NULLHANDLE || hwndClient != NULLHANDLE) {
        return FALSE ;
    }

    /*
     * create standard window (set)
     */

    flFrameStyle = 0 ;
    flFrameFlags = FCF_TASKLIST | FCF_TITLEBAR | FCF_ICON | FCF_SYSMENU |
                   FCF_SIZEBORDER | FCF_MINBUTTON | FCF_MAXBUTTON |
                   FCF_HORZSCROLL | FCF_VERTSCROLL | FCF_ACCELTABLE ;

    WinRegisterClass(hab, VNCPROC, procClient, CS_SIZEREDRAW, 0) ;

    hwndFrame = WinCreateStdWindow(
            HWND_DESKTOP,           /* parent window handle     */
            flFrameStyle,           /* frame window styles      */
            &flFrameFlags,          /* create flags             */
            VNCPROC,                /* client window class      */
            ProgramName,            /* as title                 */
            0,                      /* client window styles     */
            NULLHANDLE,             /* resource (internal)      */
            ID_PMVNC,               /* window ID                */
            &hwndClient) ;          /* client window            */

    if (hwndFrame == NULLHANDLE || hwndClient == NULLHANDLE) {
        return FALSE ;
    }

    pfnFrame = WinSubclassWindow(hwndFrame, procFrame) ;

    WinStartTimer(hab, hwndClient, 1, 50) ;

    WinQueryWindowPos(hwndFrame,  &swpFrame)  ;
    WinQueryWindowPos(hwndClient, &swpClient) ;

    hwndHorz = WinWindowFromID(hwndFrame, FID_HORZSCROLL) ;
    hwndVert = WinWindowFromID(hwndFrame, FID_VERTSCROLL) ;

    menuInit(hwndFrame)  ;      /* expand system menu   */
    kstCreate(hwndFrame) ;      /* add modal key state  */

    sv_cxScreen     = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN)     ;
    sv_cyScreen     = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN)     ;
    sv_cyTitleBar   = WinQuerySysValue(HWND_DESKTOP, SV_CYTITLEBAR)   ;
    sv_cxSizeBorder = WinQuerySysValue(HWND_DESKTOP, SV_CXSIZEBORDER) ;
    sv_cySizeBorder = WinQuerySysValue(HWND_DESKTOP, SV_CYSIZEBORDER) ;

    return TRUE ;
}

/*
 * winDispose - dispose windows
 */

void    winDispose(HAB hab)
{
    WinStopTimer(hab, hwndClient, 1) ;

    kstDestroy() ;

    if (hwndClient != NULLHANDLE) {
        WinDestroyWindow(hwndClient) ;
        hwndClient = NULLHANDLE ;
    }
    if (hwndFrame != NULLHANDLE) {
        WinDestroyWindow(hwndFrame) ;
        hwndFrame = NULLHANDLE ;
    }
}
