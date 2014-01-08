/*
 * keymap.c - PM VNC Viewer, user defined scan key mapping
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "keysymdef.h"

/*
 * Mapping Table, Scan KeyCode -> XKeySym
 */

static  ULONG   usermap[256] ;
static  BOOL    first = TRUE ;

/*
 * kmapLoad - load user define keymap from file
 */

BOOL    kmapLoad(PUCHAR name)
{
    int     i   ;
    FILE    *fp ;
    UCHAR   line[256]  ;
    ULONG   scan, xkey ;

    if (first) {
        for (i = 0 ; i < 256 ; i++) {
            usermap[i] = XK_VoidSymbol ;
        }
        first = FALSE ;
    }
    if ((fp = fopen(name, "r")) == NULL) {
        return FALSE ;
    }

    while (fgets(line, 256, fp) != NULL) {
        if (line[0] == '#') {
            continue ;
        }
        if (sscanf(line, "%x %x", &scan, &xkey) != 2) {
            continue ;
        }
        if (scan >= 256) {
            continue ;
        }
#ifdef DEBUG
        TRACE("Map %x -> %x \n", scan, xkey) ;
#endif
        usermap[scan] = xkey ;
    }
    fclose(fp)  ;

#ifdef  DEBUG
    for (i = 0 ; i < 256 ; i++) {
        if (usermap[i] != XK_VoidSymbol) {
            TRACE("%02x -> %x\n", i, usermap[i]) ;
        }
    }
#endif
    return TRUE ;
}

/*
 * kmapFree - dispose user keymap (nothing to do now)
 */

void    kmapFree(void)
{
    return ;
}

/*
 * kmapQuery - query user keymap
 */

ULONG   kmapQuery(USHORT sc)
{
    int     i ;

    if (first) {
        for (i = 0 ; i < 256 ; i++) {
            usermap[i] = XK_VoidSymbol ;
        }
        first = FALSE ;
    }

#ifdef DEBUG
    TRACE("kmapQuery D:%d H:%02x %x\n", sc, sc, usermap[sc & 0xff]) ;
#endif
    return usermap[sc & 0xff];
}


