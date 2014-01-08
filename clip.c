/*
 * clip.c - PM VNC Viewer, Clipboard Handling
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"

static  PUCHAR  serverCutText = NULL ;

/*
 * clipHold - hold 'Server Cut Text'
 */

void    clipHold(HWND hwnd, PUCHAR text)
{
    if (serverCutText) {
        free(serverCutText) ;
    }
    serverCutText = text ;
}

/*
 * clipChkLocalClip - check if local clip text exist
 */

BOOL    clipChkLocalClip(HWND hwnd)
{
    HAB     hab  ;
    BOOL    stat ;

    hab = WinQueryAnchorBlock(hwnd) ;
    WinOpenClipbrd(hab)  ;
    if (WinQueryClipbrdData(hab, CF_TEXT) == 0) {
        stat = FALSE ;
    } else {
        stat = TRUE ;
    }
    WinCloseClipbrd(hab) ;
    return TRUE ;
}

/*
 * clipChkRemoteClip - check if remote clip text exist
 */

BOOL    clipChkRemoteClip(HWND hwnd)
{
    return (serverCutText != NULL) ? TRUE : FALSE ;
} 

/*
 * clipCopy - copy Server Cut Text to local clipboard
 */

void    clipCopy(HWND hwnd)
{
    PUCHAR  shm  ;
    ULONG   leng ;
    ULONG   flag ;
    HAB     hab  ;
    
    if (serverCutText == NULL) {
        return ;
    }
    
    /*
     * copy data into shared memory
     */

    leng = strlen(serverCutText) + 2 ;
    flag = OBJ_GIVEABLE | OBJ_TILE | PAG_COMMIT | PAG_READ | PAG_WRITE ;
    
    if (DosAllocSharedMem((PPVOID) &shm, NULL, leng, flag) != 0) {
        return ;
    }
    strcpy(shm, serverCutText) ;

    /*
     * set to clipboard
     */

    hab = WinQueryAnchorBlock(hwnd) ;
    WinOpenClipbrd(hab)  ;
    WinEmptyClipbrd(hab) ;
    WinSetClipbrdData(hab, (ULONG) shm, CF_TEXT, CFI_POINTER) ;
    WinCloseClipbrd(hab) ;
}

/*
 * clipPaste - paste local clipboard to remote machine
 */

void    clipPaste(HWND hwnd)
{
    HAB     hab  ;
    PUCHAR  ptr  ;

    hab = WinQueryAnchorBlock(hwnd) ;
    WinOpenClipbrd(hab)  ;
    if ((ptr = (PUCHAR) WinQueryClipbrdData(hab, CF_TEXT)) != NULL) {
        protoSendCutText(ptr) ;
    }
    WinCloseClipbrd(hab) ;
}
 
