/*
 * mouse.c - PM VNC Viewer, handling pointer events
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "pmvncres.h"

/*
 * mouse control variables
 *
 *      for center button emulation
 */

static  HPOINTER    hptrMouse = NULLHANDLE ;    /* mouse pointer    */
static  POINTL          ptLast   ;
static  struct timeval  tvLast   ;
static  USHORT          fsCheck  ;
static  USHORT          fsState  ;
static  USHORT          fsNotify ;

/*
 * mouseInit - initialize mouse handling
 */
 
void    mouseInit(HWND hwnd)
{
    hptrMouse = WinLoadPointer(HWND_DESKTOP, NULLHANDLE, ID_CURSOR) ;
    fsCheck = fsState = fsNotify = 0 ;
    ptLast.x = ptLast.y = 0 ;
    gettimeofday(&tvLast, NULL) ;
}

/*
 * mouseDone - finialize mouse handling
 */
 
void    mouseDone(HWND hwnd)
{
    if (hptrMouse != NULLHANDLE) {
        WinDestroyPointer(hptrMouse) ;
    }
}

/*
 * chkMoved - check if point moved
 */

#define MOVELIM     8

static  BOOL    chkMoved(PPOINTL np)
{
    LONG    horz, vert ;
    PPOINTL op = (PPOINTL) &ptLast ;
    
    horz = (op->x > np->x) ? op->x - np->x : np->x - op->x ;
    vert = (op->y > np->y) ? op->y - np->y : np->y - op->y ;
    
    if (horz > MOVELIM || vert >MOVELIM) {
        return TRUE ;
    } else {
        return FALSE ;
    }
}

/*
 * chkTimed - check if timer expired
 */

#define TIMELIM     (1000 * 50)

static  BOOL    chkTimed(void)
{
    struct timeval  tv ;
    LONG    sec, usec ;
    
    gettimeofday(&tv, NULL) ;
    if ((sec = tv.tv_sec - tvLast.tv_sec) > 2)  {
        return TRUE ;
    }
    if ((usec = sec * 1000 * 1000 + tv.tv_usec - tvLast.tv_usec) > TIMELIM) {
        return TRUE ;
    }
    return FALSE ;
}

/*
 * mouseFlush - flush pending mouse events
 */

static  void    mouseFlush(void)
{
    int     i    ;
    USHORT  state, mask ;
    
    state = fsNotify ;
    
    for (i = 0, mask = 0x01 ; i < 3 ; i++, mask <<= 1) {
        if (fsCheck & mask) {
	    if (fsState & mask) {
	        state |= mask ;
	    } else {
	        state &= ~mask ;
	    }
	}
    }
    if (state != fsNotify) {
        protoSendMouEvent((fsNotify = state), &ptLast) ;
    }
    fsCheck = 0 ;
}

/*
 * mouseMove - process MOUSEMOVE event
 */
 
void    mouseMove(HWND hwnd, PPOINTL pt)
{
    if (hptrMouse != NULLHANDLE) {
        WinSetPointer(HWND_DESKTOP, hptrMouse) ;
    }

    if (chkMoved(pt) || chkTimed()) {
        mouseFlush() ;
    }
    protoSendMouEvent(fsNotify, pt)  ;

    gettimeofday(&tvLast, NULL) ;
    ptLast = *pt ;
}

/*
 * mouseTimer - timer entry for mouse handling
 */
    
void    mouseTimer(HWND hwnd)
{
    if (fsCheck && chkTimed()) {
        mouseFlush() ;
    }
    return ;
}

/*
 * mouseButtonDn - button down
 */

static  void    mouseButtonDn(HWND hwnd, ULONG msg, PPOINTL pt)
{
    USHORT  button ;
    
    if (chkMoved(pt) || chkTimed()) {
        mouseFlush() ;
    }
    
    switch (msg) {
        case WM_BUTTON1DOWN : button = 0x0001 ; break ;
        case WM_BUTTON2DOWN : button = 0x0004 ; break ;
        case WM_BUTTON3DOWN : button = 0x0002 ; break ;
        default             : button = 0x0000 ; break ;
    }
    if (button == 0) {
        return ;
    }
    fsState |= button ;
    
    if ((fsCheck & 0x05) && ((fsState & 0x05) == 0x05)) {
        protoSendMouEvent((fsNotify |= 0x02), pt) ;
	gettimeofday(&tvLast, NULL) ;
	ptLast = *pt ;
	fsCheck = 0 ;
        return ;
    }
    fsCheck |= button ;
    gettimeofday(&tvLast, NULL) ;
    ptLast = *pt ;
}

/*
 * mouseButtonUp - button up
 */

static  void    mouseButtonUp(HWND hwnd, ULONG msg, PPOINTL pt)
{
    USHORT  button ;
    
    if (chkMoved(pt) || chkTimed()) {
        mouseFlush() ;
    }
    
    switch (msg) {
        case WM_BUTTON1UP : button = 0x0001 ; break ;
        case WM_BUTTON2UP : button = 0x0004 ; break ;
        case WM_BUTTON3UP : button = 0x0002 ; break ;
        default           : button = 0x0000 ; break ;
    }
    if (button == 0) {
        return ;
    }
    fsState &= ~button ;

    if (fsNotify & 0x0002) {
        if ((fsState & 0x0005) == 0) {
            fsNotify &= ~0x0002 ;
	    protoSendMouEvent(fsNotify, pt) ; 
	}
    } else if (fsNotify & button) {
        fsNotify &= ~button ;
	protoSendMouEvent(fsNotify, pt) ;
    }
    gettimeofday(&tvLast, NULL) ;
    ptLast = *pt ;
}

/*
 * mouseButtonClick - button click
 *      Click apprears after DOWN, UP, so confirms button is up.
 */

static  void    mouseButtonClick(HWND hwnd, ULONG msg, PPOINTL pt)
{
    USHORT  button ;
    
    if (chkMoved(pt) || chkTimed()) {
        mouseFlush() ;
    }

    switch (msg) {
        case WM_BUTTON1CLICK : button = 0x0001 ; break ;
        case WM_BUTTON2CLICK : button = 0x0004 ; break ;
        case WM_BUTTON3CLICK : button = 0x0002 ; break ;
        default              : button = 0x0000 ; break ;
    }
    if (button == 0) {
        return ;
    }
    fsState &= ~button ;

    if (fsNotify & 0x0002) {
        if ((fsState & 0x0005) == 0) {
            fsNotify &= ~0x0002 ;
	    protoSendMouEvent(fsNotify, pt) ;
	}
    } else if (fsNotify & button) {
        fsNotify &= ~button ;
	protoSendMouEvent(fsNotify, pt) ;
    }
    gettimeofday(&tvLast, NULL) ;
    ptLast = *pt ;
}

/*
 * mouseButtonDblClk - button double click
 *      Double Click as DOWN, UP, DBLCLK, UP, so do same as Down Notify
 */

static  void    mouseButtonDblClk(HWND hwnd, ULONG msg, PPOINTL pt)
{
    USHORT  button ;
    
    if (chkMoved(pt) || chkTimed()) {
        mouseFlush() ;
    }

    switch (msg) {
        case WM_BUTTON1DBLCLK : button = 0x0001 ; break ;
        case WM_BUTTON2DBLCLK : button = 0x0004 ; break ;
        case WM_BUTTON3DBLCLK : button = 0x0002 ; break ;
        default               : button = 0x0000 ; break ;
    }
    if (button == 0) {
        return ;
    }
    fsState |= button ;
    
    if ((fsCheck & 0x05) && ((fsState & 0x05) == 0x05)) {
        protoSendMouEvent((fsNotify |= 0x02), pt) ;
	gettimeofday(&tvLast, NULL) ;
	ptLast = *pt ;
	fsCheck = 0  ;
        return ;
    }
    fsCheck |= button ;
    gettimeofday(&tvLast, NULL) ;
    ptLast = *pt ;
}

/*
 * mouseEvent - mouse down/up
 */

void    mouseEvent(HWND hwnd, ULONG msg, PPOINTL pt)
{
    switch (msg) {
    case WM_BUTTON1DOWN :
    case WM_BUTTON2DOWN :
    case WM_BUTTON3DOWN :
        mouseButtonDn(hwnd, msg, pt) ;
	break ;
    case WM_BUTTON1UP   :
    case WM_BUTTON2UP   :
    case WM_BUTTON3UP   :
        mouseButtonUp(hwnd, msg, pt) ;
	break ;
    case WM_BUTTON1CLICK :
    case WM_BUTTON2CLICK :
    case WM_BUTTON3CLICK :
        mouseButtonClick(hwnd, msg, pt) ;
	break ;
    case WM_BUTTON1DBLCLK :
    case WM_BUTTON2DBLCLK :
    case WM_BUTTON3DBLCLK :
        mouseButtonDblClk(hwnd, msg, pt) ;
	break ;
    }
    return ;
}
