/*
 * querycap.c - query device capability
 */

#include <stdio.h>
#include <stdlib.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"

int     queryColorIndex(void)
{
    HDC     hdc ;
    LONG    num ;

    hdc = DevOpenDC(habNetwork, OD_MEMORY, "*", 0, NULL, NULLHANDLE) ;
    if (hdc == NULLHANDLE) {

#ifdef DEBUG
        TRACE("queryColorIndex - NULL hdc\n") ;
#endif

        return 0 ;
    }
    if (DevQueryCaps(hdc, CAPS_COLOR_INDEX, 1, &num) != TRUE) {

#ifdef DEBUG
        TRACE("queryColorIndex - failed\n") ;
#endif

        return 0 ;
    }
    num += 1 ;

#ifdef DEBUG
    TRACE("queryColorIndex - %d\n", num) ;
#endif

    DevCloseDC(hdc) ;

    return (int) num ;
}
