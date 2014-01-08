/*
 * keyst.c - PM VNC Viewer, Modal key status window
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "pmvncres.h"

/*
 * use two status windows
 */

static  HWND    hwndStateCtl = NULLHANDLE ;
static  HWND    hwndStateAlt = NULLHANDLE ;

#define WM_KST_UPDATE   (WM_USER + 1)
#define WM_KST_KEYDN    (WM_USER + 2)
#define WM_KST_KEYUP    (WM_USER + 3)

/*
 * control data for Modal Key Status
 */

typedef struct _KSTREC {
    UCHAR   keyName[8] ;
    BOOL    keyDown    ;
    COLOR   clrFore    ;
    COLOR   clrDn      ;
    COLOR   clrUp      ;
    HWND    hwndMn     ;
    HWND    hwndEv     ;
    SHORT   evKeyTgl   ;
    SHORT   evKeyDn    ;
    SHORT   evKeyUp    ;
} KSTREC, *KSTPTR ;

static  KSTREC  kstCtl = {
    "CTL",
    FALSE,
    CLR_BLACK,
    CLR_WHITE,
    CLR_DARKGRAY,
    NULLHANDLE,
    NULLHANDLE,
    IDM_CTLTGL,
    IDM_CTLDN,
    IDM_CTLUP
} ;

static  KSTREC  kstAlt = {
    "ALT",
    FALSE,
    CLR_BLACK,
    CLR_WHITE,
    CLR_DARKGRAY,
    NULLHANDLE,
    NULLHANDLE,
    IDM_ALTTGL,
    IDM_ALTDN,
    IDM_ALTUP
} ;

/*
 * kstDraw - draw modal key state
 */

static  void    kstDraw(HWND hwnd, KSTPTR p)
{
    HPS     hps    ;
    RECTL   rct    ;
    LONG    fore, back ;

#ifdef DEBUG
    TRACE("kstDraw %s\n", p->keyName) ; fflush(stdout) ;
#endif
    fore = p->clrFore ;
    back = p->keyDown ? p->clrDn : p->clrUp ;
    hps = WinBeginPaint(hwnd, NULLHANDLE, NULL) ;
    WinQueryWindowRect(hwnd, &rct) ;
    WinDrawText(hps, strlen(p->keyName), p->keyName, &rct,
                    fore, back, (DT_CENTER | DT_VCENTER | DT_ERASERECT)) ;
    WinEndPaint(hps) ;
}

/*
 * kstSend - Send Event
 */

static  void    kstSend(HWND hwnd, KSTPTR p, SHORT cmd)
{
    SHORT   ev ;

#ifdef DEBUG
    TRACE("kstSend %s %d\n", p->keyName, cmd) ;
#endif

    switch (cmd) {
        case IDM_KEYTGL : ev = p->evKeyTgl ; break ;
        case IDM_KEYDN  : ev = p->evKeyDn  ; break ;
        case IDM_KEYUP  : ev = p->evKeyUp  ; break ;
        default         : ev = 0           ; break ;
    }
    if (p->hwndEv == NULLHANDLE || ev == 0) {
        return ;
    }
    WinSendMsg(p->hwndEv, WM_COMMAND, MPFROM2SHORT(ev, 0), NULL) ;
}

/*
 * kstMenu - context menu on Status Window
 */

static  void    kstMenu(HWND hwnd, KSTPTR p)
{
    POINTL  pt   ;
    ULONG   opts ;

    if (p->hwndMn == NULLHANDLE) {
        p->hwndMn = WinLoadMenu(hwnd, NULLHANDLE, IDM_KEYST) ;
    }
    if (p->hwndMn == NULLHANDLE) {
        return ;
    }

    WinQueryPointerPos(HWND_DESKTOP, &pt) ;

    opts = PU_HCONSTRAIN | PU_VCONSTRAIN |
             PU_KEYBOARD | PU_MOUSEBUTTON1 | PU_MOUSEBUTTON2 ;

    WinPopupMenu(HWND_DESKTOP, hwnd, p->hwndMn,
                                     pt.x, pt.y, IDM_KEYTGL, opts) ;
}

/*
 * kstProc - window procedure for Modal Key Status window
 */

static  UCHAR   kstName[] = "KeyStateWindow" ;

static MRESULT EXPENTRY kstProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    KSTPTR  p      ;
//    HPS     hps    ;
//    RECTL   rct    ;
//    UCHAR   buf[2] ;

    p = (KSTPTR) WinQueryWindowPtr(hwnd, 0) ;

#ifdef DEBUG
    TRACE("keyst  %08x\n", msg) ;
#endif

    switch (msg) {

    case WM_CREATE :
        WinSetWindowPtr(hwnd, 0, (PVOID) mp1) ;
        return (MRESULT) 0 ;

    case WM_PAINT :
        kstDraw(hwnd, p) ;
        return (MRESULT) 0 ;

    case WM_SINGLESELECT :
        kstSend(hwnd, p, IDM_KEYTGL) ;
        return (MRESULT) 0 ;

    case WM_CONTEXTMENU  :
        kstMenu(hwnd, p) ;
        return (MRESULT) 0 ;

    case WM_COMMAND :
        kstSend(hwnd, p, SHORT1FROMMP(mp1)) ;
        return (MRESULT) 0 ;

    case WM_KST_UPDATE :
        WinInvalidateRect(hwnd, NULL, FALSE) ;
        return (MRESULT) 0 ;

    case WM_KST_KEYDN :
    case WM_KST_KEYUP :
        p->keyDown = (msg == WM_KST_KEYDN) ? TRUE : FALSE ;
        WinInvalidateRect(hwnd, NULL, FALSE) ;
        return (MRESULT) 0 ;
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2) ;
}

/*
 * kstDestroy - destroy key state windows
 */

void    kstDestroy(void)
{
    if (hwndStateCtl != NULLHANDLE) {
        WinDestroyWindow(hwndStateCtl) ;
        hwndStateCtl = NULLHANDLE ;
    }
    if (hwndStateAlt != NULLHANDLE) {
        WinDestroyWindow(hwndStateAlt) ;
        hwndStateAlt = NULLHANDLE ;
    }
}

/*
 * kstCreate - create key state windows
 */

BOOL    kstCreate(HWND hwndFrame)
{
    HWND    hwndTitle = WinWindowFromID(hwndFrame, FID_TITLEBAR) ;
    ULONG   flStyle ;

#ifdef DEBUG
    TRACE("kstCreate\n") ;
#endif

    /*
     * notify to Frame Window
     */

    kstCtl.hwndEv = hwndFrame ;
    kstAlt.hwndEv = hwndFrame ;

    /*
     * Disable title-bar to paint over siblings
     */

    flStyle = WinQueryWindowULong(hwndTitle, QWL_STYLE) ;
    flStyle |= WS_CLIPCHILDREN ;
    WinSetWindowULong(hwndTitle, QWL_STYLE, flStyle) ;

    /*
     * Create Modal Key State Windows
     */

    WinRegisterClass(WinQueryAnchorBlock(hwndFrame), kstName, kstProc,
            (CS_CLIPSIBLINGS | CS_SYNCPAINT), sizeof(PVOID)) ;

    hwndStateCtl = WinCreateWindow(
            hwndTitle,
            kstName,
            NULL,
            0,
            0, 0, 0, 0,
            hwndFrame,
            HWND_TOP,
            IDW_CTLST,
            &kstCtl,
            NULL) ;
    hwndStateAlt = WinCreateWindow(
            hwndTitle,
            kstName,
            NULL,
            0,
            0, 0, 0, 0,
            hwndFrame,
            HWND_TOP,
            IDW_ALTST,
            &kstAlt,
            NULL) ;

    if (hwndStateCtl == NULLHANDLE || hwndStateAlt == NULLHANDLE) {

#ifdef DEBUG
        TRACE("kstCreate - failed %08x, %08x\n", hwndStateCtl, hwndStateAlt) ;
#endif

        kstDestroy() ;
        return FALSE ;
    }
    return TRUE ;
}

/*
 * kstAdjust - re-position to title window
 */

void    kstAdjust(HWND hwndFrame)
{
    HWND    hwndTitle = WinWindowFromID(hwndFrame, FID_TITLEBAR) ;
    SWP     swpTitle, swpCtl, swpAlt ;
    int     w, h ;

    WinQueryWindowPos(hwndTitle, &swpTitle) ;

#ifdef DEBUG
    TRACE("kstAdjust : Title (%d,%d), %dx%d\n",
            swpTitle.x, swpTitle.y, swpTitle.cx, swpTitle.cy) ;
#endif

    if (swpTitle.cx == 0 || swpTitle.cy == 0) {

#ifdef DEBUG
        TRACE("kstAdjust - not visible title\n") ;
#endif
        return ;
    }
    if ((h = swpTitle.cy - 4) <= 0) {

#ifdef DEBUG
        TRACE("kstAdjust - not enough title height\n") ;
#endif

        return ;
    }
    w = h * 2 ;

    swpCtl.x = swpTitle.cx - (w + 2) * 2 ;
    if (swpCtl.x < 0) {

#ifdef DEBUG
        TRACE("kstAdjust - not enough width\n") ;
#endif

        return ;
    }
    swpCtl.y = 2 ;
    if (swpCtl.y < 0) {

#ifdef DEBUG
        TRACE("kstAdjust - not enough height\n") ;
#endif

        return ;
    }
    swpCtl.cx = w ;
    swpCtl.cy = h ;

    swpAlt.x = swpCtl.x + (w + 2) ;
    swpAlt.y = swpCtl.y ;
    swpAlt.cx = w ;
    swpAlt.cy = h ;

    WinSetWindowPos(hwndStateCtl, HWND_TOP,
            swpCtl.x, swpCtl.y, swpCtl.cx, swpCtl.cy,
            SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ZORDER) ;
    WinSetWindowPos(hwndStateAlt, HWND_TOP,
            swpAlt.x, swpAlt.y, swpAlt.cx, swpAlt.cy,
            SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ZORDER) ;

    WinPostMsg(hwndStateCtl, WM_KST_UPDATE, NULL, NULL) ;
    WinPostMsg(hwndStateAlt, WM_KST_UPDATE, NULL, NULL) ;
}

/*
 * kstCtlState, kstAltState - state change
 */

void    kstCtlState(BOOL down)
{
    ULONG   msg ;
    msg = down ? WM_KST_KEYDN : WM_KST_KEYUP  ;
    WinSendMsg(hwndStateCtl, msg, NULL, NULL) ;
}

void    kstAltState(BOOL down)
{
    ULONG   msg ;
    msg = down ? WM_KST_KEYDN : WM_KST_KEYUP  ;
    WinSendMsg(hwndStateAlt, msg, NULL, NULL) ;
}
