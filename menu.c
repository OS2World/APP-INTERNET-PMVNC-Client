/*
 * menu.c - PM VNC Viewer, Menu Handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "pmvncres.h"

extern void infoAbout(HWND hwnd);
extern void infoConn(void);

/*
 * Variables to Manage Menus
 */

static  HWND    hwndSysMenu = NULLHANDLE ;
static  HWND    hwndSubMenu = NULLHANDLE ;
static  USHORT  fidSubMenu ;

/*
 * menuInit - append additional menus to SysMenu
 */

BOOL    menuInit(HWND hwndFrame)
{
    HWND        hwndNewMenu ;
    MENUITEM    miSysMenu   ;
    MENUITEM    miNewMenu   ;
    int         i, id ;
    PUCHAR      nmMenu[128] ;

    hwndSysMenu = WinWindowFromID(hwndFrame, FID_SYSMENU) ;
    fidSubMenu = (USHORT) WinSendMsg(hwndSysMenu,
                MM_ITEMIDFROMPOSITION, MPFROMSHORT(0), NULL) ;
    WinSendMsg(hwndSysMenu, MM_QUERYITEM,
            MPFROM2SHORT(fidSubMenu, FALSE), MPFROMP(&miSysMenu)) ;
    hwndSubMenu = miSysMenu.hwndSubMenu ;

#ifdef DEBUG
    TRACE("SysSubMenu %d (%08x)\n", fidSubMenu, fidSubMenu) ;
#endif

    if (hwndSysMenu == NULLHANDLE || hwndSubMenu == NULLHANDLE) {

#ifdef DEBUG
        TRACE("menuInit - failed to access to SysMenu\n") ;
#endif

        return FALSE ;
    }

    hwndNewMenu = WinLoadMenu(hwndSubMenu, NULLHANDLE, IDM_MENU) ;

    if (hwndNewMenu == NULLHANDLE) {

#ifdef DEBUG
        TRACE("menuInit - failed to load new menu\n") ;
#endif

        return FALSE ;
    }

    for (i = 0 ; ; i++) {
        id = (USHORT) WinSendMsg(hwndNewMenu, MM_ITEMIDFROMPOSITION,
                MPFROMSHORT(i), NULL) ;
        WinSendMsg(hwndNewMenu, MM_QUERYITEM,
                MPFROM2SHORT(id, FALSE), MPFROMP(&miNewMenu)) ;
        WinSendMsg(hwndNewMenu, MM_QUERYITEMTEXT,
                MPFROM2SHORT(id, 128), MPFROMP(nmMenu)) ;
        miNewMenu.iPosition = MIT_END ;
        WinSendMsg(hwndSubMenu, MM_INSERTITEM,
                MPFROMP(&miNewMenu), MPFROMP(nmMenu)) ;
        if (id == IDM_ABOUT) {
            break ;
        }
    }
    return TRUE ;
}

/*
 * menuHook - setup menu items (on WM_INITMENU)
 */

void    menuHook(HWND hwnd, USHORT id)
{
    USHORT  attr, mask ;

#ifdef DEBUG
    TRACE("menuHook %08x (%08x)\n", id, fidSubMenu) ;
#endif

    if (id != fidSubMenu) {
        return ;
    }

#ifdef DEBUG
    TRACE("menuHook for SysSubMenu\n") ;
#endif

    mask = MIA_DISABLED ;
    if (clipChkRemoteClip(hwnd)) {
        attr = ~MIA_DISABLED ;
    } else {
        attr = MIA_DISABLED ;
    }
    WinSendMsg(hwndSubMenu, MM_SETITEMATTR,
        MPFROM2SHORT(IDM_COPY, FALSE), MPFROM2SHORT(mask, attr)) ;

#ifdef DEBUG
    TRACE("IDM_COPY %04x\n", (mask & attr)) ;
#endif

    mask = MIA_DISABLED ;
    if (clipChkLocalClip(hwnd)) {
        attr = ~MIA_DISABLED ;
    } else {
        attr = MIA_DISABLED ;
    }
    WinSendMsg(hwndSubMenu, MM_SETITEMATTR,
        MPFROM2SHORT(IDM_PASTE, FALSE), MPFROM2SHORT(mask, attr)) ;

#ifdef DEBUG
    TRACE("IDM_PASTE %04x\n", (mask & attr)) ;
#endif
}

/*
 * menuProc - handle notify from menu (on WM_COMMAND)
 */

void    menuProc(HWND hwnd, USHORT id)
{
    switch (id) {

    case IDM_OPTIONS :
        sessModify(WinQueryAnchorBlock(hwnd)) ;
        break ;
    case IDM_CONNINF :
        infoConn() ;
        break ;
    case IDM_ABOUT   :
        infoAbout(hwnd) ;
        break ;
    case IDM_REFRESH :
        protoSendRequest(FALSE, NULL) ;
        break ;
    case IDM_COPY    :
        clipCopy(hwnd) ;
        break ;
    case IDM_PASTE   :
        clipPaste(hwnd) ;
        break ;
    case IDM_CTLTGL  :
    case IDM_ALTTGL  :
        keyEmulateToggle(id) ;
        break ;
    case IDM_CTLDN   :
    case IDM_CTLUP   :
    case IDM_ALTDN   :
    case IDM_ALTUP   :
        keyEmulateFixed(id) ;
        break ;
    case IDM_FUNC01  :
    case IDM_FUNC02  :
    case IDM_FUNC03  :
    case IDM_FUNC04  :
    case IDM_FUNC05  :
    case IDM_FUNC06  :
    case IDM_FUNC07  :
    case IDM_FUNC08  :
    case IDM_FUNC09  :
    case IDM_FUNC10  :
    case IDM_FUNC11  :
    case IDM_FUNC12  :
        keyEmulateFixed(id) ;
        break ;
    case IDM_C_A_D   :
    case IDM_CTLESC  :
    case IDM_ALTTAB  :
        keyEmulateFixed(id) ;
        break ;
    }
}
