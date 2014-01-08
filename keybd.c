/*
 * keybd.c - PM VNC Viewer, handling keyboard events
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "pmvncres.h"

#define XK_MISCELLANY
#define XK_XKB_KEYS
#define XK_LATIN1
#define XK_LATIN2
#define XK_LATIN3
#define XK_LATIN4
#define XK_GREEK

#include "keysymdef.h"

/*
 * Managing Modal Key (CTL & ALT)
 */

static  BOOL    stateSftDown = FALSE ;
static  BOOL    stateCtlDown = FALSE ;
static  BOOL    stateAltDown = FALSE ;

static BOOL keyMapped(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc);
static void keyVirtual(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc);
static void keyControl(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc);
static void keyNormal(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc);
static void sendKeyEvent(BOOL down, ULONG xkey);

/*
 * Mapping from PM Virtual Key to X Key
 */

static  ULONG   vkMap[] = {
    /* 00 - undef           */  XK_VoidSymbol,
    /* 01 - VK_BUTTON1      */  XK_VoidSymbol,
    /* 02 - VK_BUTTON2      */  XK_VoidSymbol,
    /* 03 - VK_BUTTON3      */  XK_VoidSymbol,
    /* 04 - VK_BREAK        */  XK_Break,
    /* 05 - VK_BACKSPACE    */  XK_BackSpace,
    /* 06 - VK_TAB          */  XK_Tab,
    /* 07 - VK_BACKTAB      */  XK_VoidSymbol,
    /* 08 - VK_NEWLINE      */  XK_Return,
    /* 09 - VK_SHIFT        */  XK_Shift_L,
    /* 0a - VK_CTRL         */  XK_Control_L,
    /* 0b - VK_ALT          */  XK_VoidSymbol,
    /* 0c - VK_ALTGRAF      */  XK_VoidSymbol,
    /* 0d - VK_PAUSE        */  XK_Pause,
    /* 0e - VK_CAPSLOCK     */  XK_Caps_Lock,
    /* 0f - VK_ESC          */  XK_Escape,
    /* 10 - VK_SPACE        */  XK_space,
    /* 11 - VK_PAGEUP       */  XK_Page_Up,
    /* 12 - VK_PAGEDOWN     */  XK_Page_Down,
    /* 13 - VK_END          */  XK_End,
    /* 14 - VK_HOME         */  XK_Home,
    /* 15 - VK_LEFT         */  XK_Left,
    /* 16 - VK_UP           */  XK_Up,
    /* 17 - VK_RIGHT        */  XK_Right,
    /* 18 - VK_DOWN         */  XK_Down,
    /* 19 - VK_PRINTSCRN    */  XK_Print,
    /* 1a - VK_INSERT       */  XK_Insert,
    /* 1b - VK_DELETE       */  XK_Delete,
    /* 1c - VK_SCRLLOCK     */  XK_Scroll_Lock,
    /* 1d - VK_NUMLOCK      */  XK_Num_Lock,
    /* 1e - VK_ENTER        */  XK_Return,
    /* 1f - VK_SYSRQ        */  XK_Sys_Req,
    /* 20 - VK_F1           */  XK_F1,
    /* 21 - VK_F2           */  XK_F2,
    /* 22 - VK_F3           */  XK_F3,
    /* 23 - VK_F4           */  XK_F4,
    /* 24 - VK_F5           */  XK_F5,
    /* 25 - VK_F6           */  XK_F6,
    /* 26 - VK_F7           */  XK_F7,
    /* 27 - VK_F8           */  XK_F8,
    /* 28 - VK_F9           */  XK_F9,
    /* 29 - VK_F10          */  XK_F10,
    /* 2a - VK_F11          */  XK_F11,
    /* 2b - VK_F12          */  XK_F12,
    /* 2c - VK_F13          */  XK_F13,
    /* 2d - VK_F14          */  XK_F14,
    /* 2e - VK_F15          */  XK_F15,
    /* 2f - VK_F16          */  XK_F16,
    /* 30 - VK_F17          */  XK_F17,
    /* 31 - VK_F18          */  XK_F18,
    /* 32 - VK_F19          */  XK_F19,
    /* 33 - VK_F20          */  XK_F20,
    /* 34 - VK_F21          */  XK_F21,
    /* 35 - VK_F22          */  XK_F22,
    /* 36 - VK_F23          */  XK_F23,
    /* 37 - VK_F24          */  XK_F24,
    /* 38 - VK_ENDDRAG      */  XK_VoidSymbol,
    /* 39 - VK_CLEAR        */  XK_Clear,
    /* 3a - VK_EREOF        */  XK_VoidSymbol,
    /* 3b - VK_PA1          */  XK_VoidSymbol,
    /* 3c - VK_ATTN         */  XK_VoidSymbol,
    /* 3d - VK_CRSEL        */  XK_VoidSymbol,
    /* 3e - VK_EXSEL        */  XK_VoidSymbol,
    /* 3f - VK_COPY         */  XK_VoidSymbol,
    /* 40 - VK_BLK1         */  XK_VoidSymbol,
    /* 41 - VK_BLK2         */  XK_VoidSymbol,
    /* 42 - undef           */  XK_VoidSymbol,
    /* 43 - undef           */  XK_VoidSymbol,
    /* 44 - undef           */  XK_VoidSymbol,
    /* 45 - undef           */  XK_VoidSymbol,
    /* 46 - undef           */  XK_VoidSymbol,
    /* 47 - undef           */  XK_VoidSymbol,
    /* 48 - undef           */  XK_VoidSymbol,
    /* 49 - undef           */  XK_VoidSymbol,
    /* 4a - undef           */  XK_VoidSymbol,
    /* 4b - undef           */  XK_VoidSymbol,
    /* 4c - undef           */  XK_VoidSymbol,
    /* 4d - undef           */  XK_VoidSymbol,
    /* 4e - undef           */  XK_VoidSymbol,
    /* 4f - undef           */  XK_VoidSymbol,
    /* 50 - undef           */  XK_VoidSymbol,
    /* 51 - undef           */  XK_VoidSymbol,
    /* 52 - undef           */  XK_VoidSymbol,
    /* 53 - undef           */  XK_VoidSymbol,
    /* 54 - undef           */  XK_VoidSymbol,
    /* 55 - undef           */  XK_VoidSymbol,
    /* 56 - undef           */  XK_VoidSymbol,
    /* 57 - undef           */  XK_VoidSymbol,
    /* 58 - undef           */  XK_VoidSymbol,
    /* 59 - undef           */  XK_VoidSymbol,
    /* 5a - undef           */  XK_VoidSymbol,
    /* 5b - undef           */  XK_VoidSymbol,
    /* 5c - undef           */  XK_VoidSymbol,
    /* 5d - undef           */  XK_VoidSymbol,
    /* 5e - undef           */  XK_VoidSymbol,
    /* 5f - undef           */  XK_VoidSymbol,
    /* 60 - undef           */  XK_VoidSymbol,
    /* 61 - undef           */  XK_VoidSymbol,
    /* 62 - undef           */  XK_VoidSymbol,
    /* 63 - undef           */  XK_VoidSymbol,
    /* 64 - undef           */  XK_VoidSymbol,
    /* 65 - undef           */  XK_VoidSymbol,
    /* 66 - undef           */  XK_VoidSymbol,
    /* 67 - undef           */  XK_VoidSymbol,
    /* 68 - undef           */  XK_VoidSymbol,
    /* 69 - undef           */  XK_VoidSymbol,
    /* 6a - undef           */  XK_VoidSymbol,
    /* 6b - undef           */  XK_VoidSymbol,
    /* 6c - undef           */  XK_VoidSymbol,
    /* 6d - undef           */  XK_VoidSymbol,
    /* 6e - undef           */  XK_VoidSymbol,
    /* 6f - undef           */  XK_VoidSymbol,
    /* 70 - undef           */  XK_VoidSymbol,
    /* 71 - undef           */  XK_VoidSymbol,
    /* 72 - undef           */  XK_VoidSymbol,
    /* 73 - undef           */  XK_VoidSymbol,
    /* 74 - undef           */  XK_VoidSymbol,
    /* 75 - undef           */  XK_VoidSymbol,
    /* 76 - undef           */  XK_VoidSymbol,
    /* 77 - undef           */  XK_VoidSymbol,
    /* 78 - undef           */  XK_VoidSymbol,
    /* 79 - undef           */  XK_VoidSymbol,
    /* 7a - undef           */  XK_VoidSymbol,
    /* 7b - undef           */  XK_VoidSymbol,
    /* 7c - undef           */  XK_VoidSymbol,
    /* 7d - undef           */  XK_VoidSymbol,
    /* 7e - undef           */  XK_VoidSymbol,
    /* 7f - undef           */  XK_VoidSymbol,
    /* 80 - undef           */  XK_Romaji,
    /* 81 - undef           */  XK_Katakana,
    /* 82 - undef           */  XK_Hiragana,
    /* 83 - undef           */  XK_Zenkaku_Hankaku,
    /* 84 - undef           */  XK_Zenkaku_Hankaku,
    /* 85 - undef           */  XK_VoidSymbol,
    /* 86 - undef           */  XK_VoidSymbol,
    /* 87 - undef           */  XK_VoidSymbol,
    /* 88 - undef           */  XK_VoidSymbol,
    /* 89 - undef           */  XK_VoidSymbol,
    /* 8a - undef           */  XK_VoidSymbol,
    /* 8b - undef           */  XK_VoidSymbol,
    /* 8c - undef           */  XK_VoidSymbol,
    /* 8d - undef           */  XK_VoidSymbol,
    /* 8e - undef           */  XK_VoidSymbol,
    /* 8f - undef           */  XK_VoidSymbol,
    /* 90 - undef           */  XK_VoidSymbol,
    /* 91 - undef           */  XK_VoidSymbol,
    /* 92 - undef           */  XK_VoidSymbol,
    /* 93 - undef           */  XK_VoidSymbol,
    /* 94 - undef           */  XK_VoidSymbol,
    /* 95 - undef           */  XK_VoidSymbol,
    /* 96 - undef           */  XK_VoidSymbol,
    /* 97 - undef           */  XK_VoidSymbol,
    /* 98 - undef           */  XK_VoidSymbol,
    /* 99 - undef           */  XK_VoidSymbol,
    /* 9a - undef           */  XK_VoidSymbol,
    /* 9b - undef           */  XK_VoidSymbol,
    /* 9c - undef           */  XK_VoidSymbol,
    /* 9d - undef           */  XK_VoidSymbol,
    /* 9e - undef           */  XK_VoidSymbol,
    /* 9f - undef           */  XK_VoidSymbol,
    /* a0 - undef           */  XK_VoidSymbol,
    /* a1 - undef           */  XK_Henkan,
    /* a2 - undef           */  XK_Muhenkan,
    /* a3 - undef           */  XK_VoidSymbol,
    /* a4 - undef           */  XK_VoidSymbol,
    /* a5 - undef           */  XK_VoidSymbol,
    /* a6 - undef           */  XK_VoidSymbol,
    /* a7 - undef           */  XK_VoidSymbol,
    /* a8 - undef           */  XK_VoidSymbol,
    /* a9 - undef           */  XK_VoidSymbol,
    /* aa - undef           */  XK_VoidSymbol,
    /* ab - undef           */  XK_VoidSymbol,
    /* ac - undef           */  XK_VoidSymbol,
    /* ad - undef           */  XK_VoidSymbol,
    /* ae - undef           */  XK_VoidSymbol,
    /* af - undef           */  XK_VoidSymbol
} ;

#define vkMax   0xaf

static ULONG KeyPadMapON[] = {
//   7   8   9   -   4   5   6   +   1   2   3   0   .
    55, 56, 57, 45, 52, 53, 54, 43, 49, 50, 51, 48, 46
};

static ULONG KeyPadMapOFF[] = {
    XK_Home,
    XK_Up,
    XK_Page_Up,
    45,
    XK_Left,
    53,
    XK_Right,
    43,
    XK_End,
    XK_Down,
    XK_Page_Down,
    XK_Insert,
    XK_Delete
};

/*
 * keyEvent - process Keyboard Events
 */
void keyEvent(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc)
{
    if (keyMapped(hwnd, flags, ch, vk, sc) == TRUE) {
        return ;
    }
    if (flags & KC_VIRTUALKEY) {
        keyVirtual(hwnd, flags, ch, vk, sc) ;
        return ;
    }
    if (flags & KC_ALT) {               /* ignore ALT combination   */
        return ;
    }
    if (flags & KC_CTRL) {
        keyControl(hwnd, flags, ch, vk, sc) ;
        return ;
    }
    if (flags & KC_CHAR) {
        keyNormal(hwnd, flags, ch, vk, sc) ;
        return ;
    }
}


/*
 * User defined mapping
 */
static BOOL keyMapped(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc)
{
    BOOL    down ;
    ULONG   xkey ;

    if ((xkey = kmapQuery(sc)) == XK_VoidSymbol) {
        return FALSE ;
    }

#ifdef DEBUG
    TRACE("Mapped %x to %x\n", sc, xkey) ;
#endif

    down = (flags & KC_KEYUP) ? FALSE : TRUE ;

    if (down && (flags & KC_PREVDOWN)) {
        sendKeyEvent(FALSE, xkey) ;
    }
    sendKeyEvent(down, xkey) ;
    return TRUE ;
}


/*
 * keyVirtual - process Virtual Key
 */
static void keyVirtual(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc)
{
    BOOL    down ;
    ULONG   xkey ;

    if (vk <= vkMax) {
        if(sc > 70 && sc < 84) {

            char KeyState[257];

            WinSetKeyboardStateTable(HWND_DESKTOP,KeyState,FALSE);

            if(KeyState[VK_NUMLOCK] & 0x01) {
                xkey = KeyPadMapON[ sc - 71 ];
            } else {
                xkey = KeyPadMapOFF[ sc - 71 ];
            }
        } else {
            xkey = vkMap[vk & 0xff] ;
        }
    } else {
        xkey = XK_VoidSymbol ;
    }
    if (xkey == XK_VoidSymbol) {
        return ;
    }
    down = (flags & KC_KEYUP) ? FALSE : TRUE ;

    if (down && (flags & KC_PREVDOWN)) {
        sendKeyEvent(FALSE, xkey) ;
    }
    sendKeyEvent(down, xkey) ;
}


/*
 * keyControl - process CTRL key pressed
 */
static void keyControl(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc)
{
    BOOL    down ;
    ULONG   xkey ;

    xkey = (ULONG) (ch & 0x1f) + 0x40 ;
    down = (flags & KC_KEYUP) ? FALSE : TRUE ;

    if (down && (flags & KC_PREVDOWN)) {
        sendKeyEvent(FALSE, xkey) ;
    }
    sendKeyEvent(down, xkey) ;
}


/*
 * keyNormal - normal (have char code) keys
 */
static void keyNormal(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc)
{
    ULONG   xkey ;

    xkey = (ULONG) (ch & 0xff) ;

    sendKeyEvent(TRUE,  xkey) ;
    sendKeyEvent(FALSE, xkey) ;
}


/*
 * keyFocusChange - adjust modal key state to current state
 */
void keyFocusChange(HWND hwnd)
{
    LONG    sft, ctl, alt ;
    BOOL    down ;

    sft = WinGetKeyState(hwnd, VK_SHIFT) ;
    ctl = WinGetKeyState(hwnd, VK_CTRL)  ;
    alt = WinGetKeyState(hwnd, VK_ALT)   ;

    down = (sft & 0x8000) ? TRUE : FALSE ;
    if (stateSftDown != down) {
        sendKeyEvent(down, XK_Shift_L) ;
    }
    down = (ctl & 0x8000) ? TRUE : FALSE ;
    if (stateCtlDown != down) {
        sendKeyEvent(down, XK_Control_L) ;
    }
    down = (alt & 0x8000) ? TRUE : FALSE ;
    if (stateAltDown != down) {
        sendKeyEvent(down, XK_Alt_L) ;
    }
}

/*
 * keyEmulateFixed - key emulation from menu (fixed sequence)
 */
typedef struct  _KEYSEQ {
    ULONG   xkey ;
    BOOL    down ;
} SEQREC, *SEQPTR ;

typedef struct  _KEYEMU {
    SHORT   id  ;
    SEQPTR  seq ;
} EMUREC, *EMUPTR ;

static  SEQREC  seqCtlDn[] = { { XK_Control_L, TRUE},  { -1, 0} } ;
static  SEQREC  seqCtlUp[] = { { XK_Control_L, FALSE}, { -1, 0} } ;
static  SEQREC  seqAltDn[] = { { XK_Alt_L,     TRUE},  { -1, 0} } ;
static  SEQREC  seqAltUp[] = { { XK_Alt_L,     FALSE}, { -1, 0} } ;

static  SEQREC  seqFunc01[] = { { XK_F1,  TRUE}, { XK_F1,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc02[] = { { XK_F2,  TRUE}, { XK_F2,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc03[] = { { XK_F3,  TRUE}, { XK_F3,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc04[] = { { XK_F4,  TRUE}, { XK_F4,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc05[] = { { XK_F5,  TRUE}, { XK_F5,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc06[] = { { XK_F6,  TRUE}, { XK_F6,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc07[] = { { XK_F7,  TRUE}, { XK_F7,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc08[] = { { XK_F8,  TRUE}, { XK_F8,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc09[] = { { XK_F9,  TRUE}, { XK_F9,  FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc10[] = { { XK_F10, TRUE}, { XK_F10, FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc11[] = { { XK_F11, TRUE}, { XK_F11, FALSE}, { -1, 0} } ;
static  SEQREC  seqFunc12[] = { { XK_F12, TRUE}, { XK_F12, FALSE}, { -1, 0} } ;

static  SEQREC  seqC_A_D[] = {
    { XK_Control_L, TRUE  } ,
    { XK_Alt_L,     TRUE  } ,
    { XK_Delete,    TRUE  } ,
    { XK_Delete,    FALSE } ,
    { XK_Alt_L,     FALSE } ,
    { XK_Control_L, FALSE } ,
    { -1, 0 }
} ;

static  SEQREC  seqCtlEsc[] = {
    { XK_Control_L, TRUE  } ,
    { XK_Escape,    TRUE  } ,
    { XK_Escape,    FALSE } ,
    { XK_Control_L, FALSE } ,
    { -1, 0 }
} ;

static  SEQREC  seqAltTab[] = {
    { XK_Alt_L, TRUE  } ,
    { XK_Tab,   TRUE  } ,
    { XK_Tab,   FALSE } ,
    { XK_Alt_L, FALSE } ,
    { -1, 0 }
} ;

static  EMUREC  emuTab[] = {
    {   IDM_CTLDN,  seqCtlDn    } ,
    {   IDM_CTLUP,  seqCtlUp    } ,
    {   IDM_ALTDN,  seqAltDn    } ,
    {   IDM_ALTUP,  seqAltUp    } ,
    {   IDM_FUNC01, seqFunc01   } ,
    {   IDM_FUNC02, seqFunc02   } ,
    {   IDM_FUNC03, seqFunc03   } ,
    {   IDM_FUNC04, seqFunc04   } ,
    {   IDM_FUNC05, seqFunc05   } ,
    {   IDM_FUNC06, seqFunc06   } ,
    {   IDM_FUNC07, seqFunc07   } ,
    {   IDM_FUNC08, seqFunc08   } ,
    {   IDM_FUNC09, seqFunc09   } ,
    {   IDM_FUNC10, seqFunc10   } ,
    {   IDM_FUNC11, seqFunc11   } ,
    {   IDM_FUNC12, seqFunc12   } ,
    {   IDM_C_A_D,  seqC_A_D    } ,
    {   IDM_CTLESC, seqCtlEsc   } ,
    {   IDM_ALTTAB, seqAltTab   } ,
    {   0,          NULL        }
} ;


void keyEmulateFixed(SHORT id)
{
    EMUPTR  ep ;
    SEQPTR  sp ;

    for (ep = emuTab ; ep->id != 0 ; ep++) {
        if (ep->id == id) {
            break ;
        }
    }
    if (ep->id != id) {
        return ;
    }

    for (sp = ep->seq ; sp->xkey != (ULONG) -1 ; sp++) {
        sendKeyEvent(sp->down, sp->xkey) ;
    }
}


/*
 * keyEmulateToggle - key emulation from menu (toggle)
 */
void keyEmulateToggle(SHORT id)
{
    ULONG   xkey ;
    BOOL    down ;

    switch (id) {
    case IDM_CTLTGL :
        xkey = XK_Control_L ;
        down = stateCtlDown ? FALSE : TRUE ;
        break ;
    case IDM_ALTTGL :
        xkey = XK_Alt_L ;
        down = stateAltDown ? FALSE : TRUE ;
        break ;
    default :
        xkey = XK_VoidSymbol ;
        break ;
    }
    if (xkey == XK_VoidSymbol) {
        return ;
    }
    sendKeyEvent(down, xkey) ;
}


static void sendKeyEvent(BOOL down, ULONG xkey)
{
    if (xkey == XK_Shift_L || xkey == XK_Shift_R) {
        stateSftDown = down ;
    }
    if (xkey == XK_Control_L || xkey == XK_Control_R) {
        kstCtlState(stateCtlDown = down) ;
    }
    if (xkey == XK_Alt_L || xkey == XK_Alt_R) {
        kstAltState(stateAltDown = down) ;
    }
    protoSendKeyEvent(down, xkey) ;
}


