/*
 * info.c - PM VNC Viewer, informations about / connection
 */

#define INCL_PM
#define INCL_DOSRESOURCES

#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pmvncdef.h"
#include "pmvncres.h"

/*
 * infoConn - connection info.
 */

static  void    loadConn(HWND hwndDlg)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_MTEXT) ;
    ULONG   len ;
    IPT     off ;
    UCHAR   info[256] ;
    rfbServerInitMsg    si ;
    PUCHAR              dp ;
    int     major, minor ;
    PUCHAR  p ;

    WinSendMsg(hwnd, MLM_DISABLEREFRESH, NULL, NULL) ;

    WinSendMsg(hwnd, MLM_FORMAT, MPFROMSHORT(MLFIE_NOTRANS), NULL) ;
    WinSendMsg(hwnd, MLM_SETTABSTOP, MPFROMSHORT(20), NULL) ;

    WinSendMsg(hwnd, MLM_SETIMPORTEXPORT, MPFROMP(info), MPFROMSHORT(256)) ;

    dp = protoConnInfo(&si, &major, &minor) ;

    len = 0, off = 0 ;

    sprintf(info, "Connected to: %s\n", dp) ;
    len = (ULONG) WinSendMsg(hwnd, MLM_IMPORT,
                            MPFROMP(&off), MPFROMLONG(strlen(info))) ;
    off += len ;

    sprintf(info, "Host: %s\n\n", SessServerName) ;
    len = (ULONG) WinSendMsg(hwnd, MLM_IMPORT,
                            MPFROMP(&off), MPFROMLONG(strlen(info))) ;
    off += len ;

    sprintf(info, "Desktop geometry: %d x %d x %d\n",
        si.framebufferWidth, si.framebufferHeight, si.format.depth) ;
    len = (ULONG) WinSendMsg(hwnd, MLM_IMPORT,
                            MPFROMP(&off), MPFROMLONG(strlen(info))) ;
    off += len ;

    switch (SessPixelFormat) {
        case PIXFMT_32   : p = "32 (TrueColor)" ; break ;
        case PIXFMT_8    : p = "8 (BGR233)"     ; break ;
        case PIXFMT_TINY : p = "8 (TinyColor)"  ; break ;
        case PIXFMT_GRAY : p = "8 (GrayScale)"  ; break ;
        default          : p = "32 (TrueColor)" ; break ;
    }
    sprintf(info, "Using depth: %s\n", p) ;
    len = (ULONG) WinSendMsg(hwnd, MLM_IMPORT,
                            MPFROMP(&off), MPFROMLONG(strlen(info))) ;
    off += len ;

    sprintf(info, "Current protocol version: %d.%d\n", major, minor) ;
    len = (ULONG) WinSendMsg(hwnd, MLM_IMPORT,
                            MPFROMP(&off), MPFROMLONG(strlen(info))) ;
    off += len ;

    WinSendMsg(hwnd, MLM_ENABLEREFRESH, NULL, NULL) ;
}

static MRESULT EXPENTRY procConn(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {

    case WM_INITDLG :
        if (SessDlgCenter) {
            dialogAtCenter(hwnd) ;
        } else {
            dialogAtMouse(hwnd, DID_OK) ;
        }
        WinSetWindowText(hwnd, "VNC connection info") ;
        loadConn(hwnd)     ;
        return (MRESULT) 0 ;

    case WM_COMMAND :
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK :
            WinDismissDlg(hwnd, DID_OK) ;
            return (MRESULT) 0 ;
        case DID_CANCEL :
            WinDismissDlg(hwnd, DID_CANCEL) ;
            return (MRESULT) 0 ;
        default :
            return (MRESULT) 0 ;
        }
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2) ;
}

void    infoConn(void)
{
    WinDlgBox(HWND_DESKTOP, HWND_DESKTOP,
            procConn, NULLHANDLE, IDD_MESG, NULL) ;
}

/*
 * infoAbout - about VNC viewer
 */
static MRESULT EXPENTRY procAbout(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch(msg)
    {
        case WM_COMMAND:
             switch(SHORT1FROMMP(mp1))
             {
                 case DID_OK:
                 case DID_CANCEL:
                      WinDismissDlg (hwnd, TRUE);
                      return 0 ;
             }
             break ;
    }

    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}


void    infoAbout(HWND hwnd)
{
    WinDlgBox(HWND_DESKTOP, hwnd,
            procAbout, NULLHANDLE, IDD_ABOUT, NULL) ;
}
