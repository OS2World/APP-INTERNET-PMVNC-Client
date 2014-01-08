/*
 * auth.c - PM VNC Viewer, authentication
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "pmvncres.h"
#include "vncauth.h"

/*
 * Password to encode challenge
 */

static  UCHAR   authPass[MAXPWLEN+2] ;

/*
 * procPass - window procedure for Password Dialog
 */

static MRESULT EXPENTRY procPass(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) 
{
    switch (msg) {
    case WM_INITDLG :
        if (SessDlgCenter) {
	    dialogAtCenter(hwnd) ;
	} else {
            dialogAtMouse(hwnd, DID_OK) ;
        }
        WinSendMsg(WinWindowFromID(hwnd, IDD_PEDIT),
	        EM_SETTEXTLIMIT, MPFROMSHORT(sizeof(authPass)), NULL) ;
        WinSetWindowText(WinWindowFromID(hwnd, IDD_PEDIT), "") ;
	return (MRESULT) 0 ;

    case WM_COMMAND :
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK :
	    memset(authPass, 0, sizeof(authPass)) ;
	    WinQueryWindowText(
	        WinWindowFromID(hwnd, IDD_PEDIT), sizeof(authPass), authPass) ;
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

/*
 * authCypher - encode challenge with password
 */

BOOL    authCypher(HWND hwnd, PUCHAR key)
{
    ULONG   ret    ;
    PUCHAR  passwd ;
    
    if (SessPasswdFile != NULL) {
        passwd = vncDecryptPasswdFromFile(SessPasswdFile) ;
	vncEncryptBytes(key, passwd) ;
	memset(passwd, 0, 8) ;
	free(passwd) ;
	return TRUE ;
    }
    
    ret = WinDlgBox(HWND_DESKTOP, hwnd,
                procPass, NULLHANDLE, IDD_PASS, NULL) ;
    if (ret != DID_OK) {
        memset(authPass, 0, sizeof(authPass)) ;
        return FALSE ;
    }
    
    vncEncryptBytes(key, authPass) ;
    memset(authPass, 0, sizeof(authPass)) ;
    return TRUE ;
}
