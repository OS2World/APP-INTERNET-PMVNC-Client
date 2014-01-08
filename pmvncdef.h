/****************************************************************************
 *
 *                  PM VNC Viewer for eComstation and OS/2
 *
 *  ========================================================================
 *
 *    Version 1.03      Michael K Greene <greenemk@cox.net>
 *                      September 2006
 *
 *                      Original PM VNC Viewer by Akira Hatakeyama
 *                      <akira@sra.co.jp>
 *
 *         Compiles with OpenWatom 1.6 and above (www.openwatcom.org).
 *
 *  ========================================================================
 *
 * Description:  pmvncdef.h - PM VNC Viewer, Global Definitions
 *
 ***************************************************************************/


#ifndef _PMVNCDEF_H
#define _PMVNCDEF_H

/*
 * Debugging Macro
 */
#ifdef  DEBUG
#define TRACE  printf
#endif

/*
 * Types required for 'rfbproto.h'
 */
typedef CHAR    INT8   ;
typedef SHORT   INT16  ;
typedef LONG    INT32  ;
typedef UCHAR   CARD8  ;
typedef USHORT  CARD16 ;
typedef ULONG   CARD32 ;

#include "rfbproto.h"

/*
 * Program Name & Profile Path
 */
extern  UCHAR   ProgramVers[] ;
extern  UCHAR   ProgramPath[] ;
extern  UCHAR   ProgramName[] ;
extern  UCHAR   ProfilePath[] ;

/*
 * Common Functions & Macros
 */
void    errMessage(PSZ msg) ;
void    dialogAtMouse(HWND hwndDialog, int target) ;
void    dialogAtCenter(HWND hwndDialog) ;

#define swap16(x)   ((((x) & 0xff) << 8) | (((x) >> 8) & 0xff))
#define swap32(x)   ((((x) & 0xff000000) >> 24) | \
                     (((x) & 0x00ff0000) >>  8) | \
                     (((x) & 0x0000ff00) <<  8) | \
                     (((x) & 0x000000ff) << 24))

/*
 * Session Setup
 */
extern  UCHAR   SessServerName[] ;
extern  CARD32  SessPreferredEncoding ;     /* see rfbproto.h   */
extern  SHORT   SessPixelFormat       ;     /* see below        */
extern  BOOL    SessOptShared         ;
extern  BOOL    SessOptViewonly       ;
extern  BOOL    SessOptDeiconify      ;
extern  BOOL    SessDlgCenter  ;    /* TRUE if dialog at center     */
extern  PUCHAR  SessPasswdFile ;    /* Name of Password File        */
extern  SIZEL   SessSavedRfb ;      /* RFB size   on last session   */
extern  POINTL  SessSavedPos ;      /* Frame pos  on last session   */
extern  SIZEL   SessSavedSiz ;      /* Frame size on last session   */
extern  POINTL  SessSavedOff ;      /* RFB offset on last session   */

#define PIXFMT_32   0       /* req. 32 bits true color, use 24 bits bitmap  */
#define PIXFMT_8    1       /* req. BGR233, use 8 bits bitmap               */
#define PIXFMT_TINY 2       /* req. BGR233, mapped to def. 16 colors        */
#define PIXFMT_GRAY 3       /* req. BGR233, mapped to 4 level gray scale    */

void    sessParse(int ac, char *av[]) ;
BOOL    sessSetup(HAB hab)  ;
BOOL    sessModify(HAB hab) ;
BOOL    sessSaveProfile(HAB hab) ;

/*
 * Windows for VNC Viewer
 */
extern  HWND    hwndFrame  ;
extern  HWND    hwndClient ;
extern  HWND    hwndHorz   ;        /* Horizontal Scroll Bar    */
extern  HWND    hwndVert   ;        /* Vertical   Scroll Bar    */

BOOL    winCreate(HAB hab)  ;
void    winDispose(HAB hab) ;

void    winShowScroll(BOOL show) ;
BOOL    winIconized(void) ;

/*
 * Messages from Network Thread (request GUI/Graphics Operation)
 */
#define WM_VNC_FAIL     (WM_USER + 1)       /* error on network     */
#define WM_VNC_AUTH     (WM_USER + 2)       /* authentication       */
#define WM_VNC_INIT     (WM_USER + 3)       /* size/name of RFB     */
#define WM_VNC_BELL     (WM_USER + 4)       /* bell                 */
#define WM_VNC_CLIP     (WM_USER + 5)       /* remote text clip     */
#define WM_VNC_UPDATE   (WM_USER + 6)       /* update on RFB        */

/*
 * Mouse Event Handlings
 */
void    mouseInit(HWND hwnd) ;
void    mouseDone(HWND hwnd) ;
void    mouseMove(HWND hwnd, PPOINTL pt) ;
void    mouseEvent(HWND hwnd, ULONG msg, PPOINTL pt) ;
void    mouseTimer(HWND hwnd) ;

/*
 * Keyboard Event Handling
 */
void    keyEvent(HWND hwnd, USHORT flags, USHORT ch, USHORT vk, USHORT sc) ;
void    keyLostFocus(HWND hwnd) ;
void    keyEmulateFixed(SHORT id) ;
void    keyEmulateToggle(SHORT id) ;

/*
 * Manages Modal Key State
 */
BOOL    kstCreate(HWND hwndFrame) ;
void    kstDestroy(void) ;
void    kstAdjust(HWND hwndFrame) ;
void    kstCtlState(BOOL down) ;
void    kstAltState(BOOL down) ;

/*
 * User Define Keymap (Scan -> XKeySym)
 */
BOOL    kmapLoad(PUCHAR name) ;
void    kmapFree(void)        ;
ULONG   kmapQuery(USHORT sc)  ;

/*
 * Menus on Windows
 */
BOOL    menuInit(HWND hwndFrame) ;
void    menuHook(HWND hwnd, USHORT id) ;    /* on WM_INITMENU   */
void    menuProc(HWND hwnd, USHORT id) ;    /* on WM_COMMAND    */

/*
 * Clipboard handlings
 */
void    clipHold(HWND hwnd, PUCHAR text) ;
BOOL    clipChkLocalClip(HWND hwnd)  ;
BOOL    clipChkRemoteClip(HWND hwnd) ;
void    clipCopy(HWND hwnd)  ;
void    clipPaste(HWND hwnd) ;

/*
 * Networking for VNC Viewer
 */
extern  HAB     habNetwork ;
extern  HMQ     hmqNetwork ;

BOOL    netStartup(HAB hab) ;
void    netFinish(HAB hab)  ;
void    netFail(PUCHAR msg) ;
void    netNotify(int ev, MPARAM mp1, MPARAM mp2) ;
BOOL    netSend(PUCHAR buf, int len) ;
BOOL    netRecv(PUCHAR buf, int len) ;
void    netDump(PUCHAR buf, int len) ;      /* for DEBUG */

/*
 * RFB Protocol Handling
 */
typedef struct _VNCREC {
    rfbPixelFormat                  rectFormat  ;
    void    (*rectDone)  (void)                 ;
    BOOL    (*rectInit)  (int cx, int cy)       ;
    BOOL    (*rectDraw)  (HPS hps, PPOINTL apt) ;
    BOOL    (*rectRaw)   (rfbRectangle *r)      ;
    BOOL    (*rectCopy)  (rfbRectangle *r)      ;
    BOOL    (*rectRRE)   (rfbRectangle *r)      ;
    BOOL    (*rectCoRRE) (rfbRectangle *r)      ;
    BOOL    (*rectTile)  (rfbRectangle *r)      ;
} VNCREC, *VNCPTR ;

int     queryColorIndex(void) ;     /* Number of color map          */

extern  VNCREC      VncCtx32    ;   /* for 32 bits true color       */
extern  VNCREC      VncCtx8     ;   /* for 8 bits BGR233            */
extern  VNCREC      VncCtx8Tiny ;   /* BGR233 with def. 16 colors   */
extern  VNCREC      VncCtx8Gray ;   /* BGR233 with 4 gray scale     */
extern  VNCREC      VncCtx4Tiny ;   /* BGR233 with def. 16 colors   */
extern  VNCREC      VncCtx4Gray ;   /* BGR233 with 4 gray scale     */
extern  VNCPTR      VncCtx   ;
extern  UCHAR       VncGreeting[] ;

BOOL    protoConnInit(void) ;
BOOL    protoSendAuth(void) ;
PUCHAR  protoConnInfo(rfbServerInitMsg *si, int *major, int *minor) ;
BOOL    protoSendFmtEnc(void) ;
BOOL    protoSendRequest(BOOL inc, PRECTL rect) ;
BOOL    protoDispatch(void) ;
BOOL    protoSendMouEvent(USHORT state, PPOINTL pt) ;
BOOL    protoSendKeyEvent(BOOL down, ULONG key)     ;
BOOL    protoSendCutText(PUCHAR text) ;

/*
 * Authentication
 */
BOOL    authCypher(HWND hwnd, PUCHAR key) ;


#endif  /* _PMVNCDEF_H */

