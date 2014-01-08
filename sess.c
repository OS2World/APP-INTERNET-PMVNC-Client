/*
 * sess.c - PM VNC Viewer, Session Setup
 */

#define INCL_PM

#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pmvncdef.h"
#include "pmvncres.h"

/*
 * VNC Session Parameters
 */

UCHAR   SessServerName[128] ;       /* as host:port */

SHORT   SessPixelFormat       = PIXFMT_32          ;
CARD32  SessPreferredEncoding = rfbEncodingHextile ;

BOOL    SessOptShared    = FALSE ;
BOOL    SessOptViewonly  = FALSE ;
BOOL    SessOptDeiconify = FALSE ;

BOOL    SessDlgCenter = FALSE ;     /* Place Dialog at Center or    */
                                    /* near mouse pointer           */

PUCHAR  SessPasswdFile = NULL ;     /* Given passwd file name       */

SIZEL   SessSavedRfb = { 0, 0 } ;   /* RFB size   on last session   */
POINTL  SessSavedPos = { 0, 0 } ;   /* Frame pos  on last session   */
SIZEL   SessSavedSiz = { 0, 0 } ;   /* Frame size on last session   */
POINTL  SessSavedOff = { 0, 0 } ;   /* RFB offset on last session   */

/*
 * options from Command Line Arguments
 *      later, marged to session parameters
 */

static  PUCHAR  argServer       = NULL  ;
static  BOOL    argFormat32     = FALSE ;
static  BOOL    argFormat8      = FALSE ;
static  BOOL    argFormatTiny   = FALSE ;
static  BOOL    argFormatGray   = FALSE ;
static  BOOL    argEncodeRaw    = FALSE ;
static  BOOL    argEncodeRre    = FALSE ;
static  BOOL    argEncodeCor    = FALSE ;
static  BOOL    argEncodeHex    = FALSE ;
static  BOOL    argOptShared    = FALSE ;
static  BOOL    argOptViewonly  = FALSE ;
static  BOOL    argOptDeiconify = FALSE ;

#define rstFormat() argFormat32=argFormat8=argFormatTiny=argFormatGray=FALSE
#define rstEncode() argEncodeRaw=argEncodeRre=argEncodeCor=argEncodeHex=FALSE

void    sessParse(int ac, char *av[])
{
    int     i ;

#ifdef DEBUG
    TRACE("SessParse\n") ;
#endif

    for (i = 1 ; i < ac ; i++) {
        if (av[i][0] != '-') {
            argServer = av[i] ;
        } else if (stricmp(&av[i][1], "true") == 0) {
            rstFormat() ;
            argFormat32 = TRUE ;
        } else if (stricmp(&av[i][1], "32bits") == 0) {
            rstFormat() ;
            argFormat32 = TRUE ;
        } else if (stricmp(&av[i][1], "bgr233") == 0) {
            rstFormat() ;
            argFormat8 = TRUE ;
        } else if (stricmp(&av[i][1], "8bits") == 0) {
            rstFormat() ;
            argFormat8 = TRUE ;
        } else if (stricmp(&av[i][1], "use8bits") == 0) {   /* compat. */
            rstFormat() ;
            argFormat8 = TRUE ;
        } else if (stricmp(&av[i][1], "tiny") == 0) {
            rstFormat() ;
            argFormatTiny = TRUE ;
        } else if (stricmp(&av[i][1], "gray") == 0) {
            rstFormat() ;
            argFormatGray = TRUE ;
        } else if (stricmp(&av[i][1], "Raw") == 0) {
            rstEncode() ;
            argEncodeRaw = TRUE ;
        } else if (stricmp(&av[i][1], "RRE") == 0) {
            rstEncode() ;
            argEncodeRre = TRUE ;
        } else if (stricmp(&av[i][1], "CoRRE") == 0) {
            rstEncode() ;
            argEncodeCor = TRUE ;
        } else if (stricmp(&av[i][1], "Hextile") == 0) {
            rstEncode() ;
            argEncodeHex = TRUE ;
        } else if (stricmp(&av[i][1], "shared") == 0) {
            argOptShared    = TRUE ;
        } else if (stricmp(&av[i][1], "viewonly") == 0) {
            argOptViewonly  = TRUE ;
        } else if (stricmp(&av[i][1], "deiconify") == 0) {
            argOptDeiconify = TRUE ;
        } else if (stricmp(&av[i][1], "dialogatcenter") == 0) {
            SessDlgCenter = TRUE  ;
        } else if (stricmp(&av[i][1], "dialogatmouse") == 0) {
            SessDlgCenter = FALSE ;
        } else if (stricmp(&av[i][1], "passwd") == 0) {
            if ((i + 1) < ac) {
                SessPasswdFile = av[i+=1] ;
            }
        } else if (stricmp(&av[i][1], "keymap") == 0) {
            if ((i + 1) < ac) {
                kmapLoad(av[i+=1]) ;
            }
        }
    }

#ifdef DEBUG
    TRACE("SessParse ... done\n") ;
#endif
}

/*
 * Session Informations in Profile
 */

typedef  struct _HOST {
    UCHAR   name[128] ;
    SHORT   format    ;
    CARD32  encode    ;
    BOOL    shared    ;
    BOOL    viewonly  ;
    BOOL    deiconify ;
    SIZEL   rfb       ;
    POINTL  pos       ;
    SIZEL   siz       ;
    POINTL  off       ;
} HOSTREC, *HOSTPTR ;

#define MAXHOSTS    8

static  UCHAR   profOrder[MAXHOSTS] ;
static  HOSTREC profHosts[MAXHOSTS] ;

/*
 * loadProfile - load profile data, merging given arguments
 */

static  BOOL    loadProfile(HAB hab)
{
    HINI    hini ;
    BOOL    stat ;
    ULONG   len  ;
    int     i, id ;
    UCHAR   key[32] ;

#ifdef DEBUG
    TRACE("loadProfile [%s]\n", ProfilePath) ;
#endif

    /*
     * load host order
     */

    if ((hini = PrfOpenProfile(hab, ProfilePath)) == NULLHANDLE) {

#ifdef DEBUG
        TRACE("loadProfile - failed to open %s\n", ProfilePath) ;
#endif

        return FALSE ;
    }

    len = sizeof(profOrder) ;
    stat = PrfQueryProfileData(hini, ProgramName, "ORDER", profOrder, &len) ;

    if (stat != TRUE || len != sizeof(profOrder)) {
        PrfCloseProfile(hini) ;
        memset(profOrder, 0xff, sizeof(profOrder)) ;

#ifdef DEBUG
        TRACE("loadProfile - failed to read Order\n") ;
#endif

        return FALSE ;
    }

    /*
     * load host informations
     */

    for (i = 0 ; i < MAXHOSTS ; i++) {
        if ((id = profOrder[i]) == 0xff) {
            continue ;
        }
        sprintf(key, "HOST%02d", id) ;
        len = sizeof(HOSTREC) ;
        stat = PrfQueryProfileData(hini,
                    ProgramName, key, &profHosts[id], &len) ;
        if (stat != TRUE || len != sizeof(HOSTREC)) {
            PrfCloseProfile(hini) ;
            memset(profOrder, 0xff, sizeof(profOrder)) ;

#ifdef DEBUG
            TRACE("loadProfile - failed to read Host Data %d\n", id) ;
#endif

            return FALSE ;
        }
    }
    PrfCloseProfile(hini) ;

    /*
     * modified with command line arguments
     */

    for (i = 0 ; i < MAXHOSTS ; i++) {
        if ((id = profOrder[i]) == 0xff) {
            continue ;
        }
        if (argFormat32) {
            profHosts[id].format = PIXFMT_32 ;
        }
        if (argFormat8) {
            profHosts[id].format = PIXFMT_8 ;
        }
        if (argFormatTiny) {
            profHosts[id].format = PIXFMT_TINY ;
        }
        if (argFormatGray) {
            profHosts[id].format = PIXFMT_GRAY ;
        }
        if (argEncodeRaw) {
            profHosts[id].encode = rfbEncodingRaw ;
        }
        if (argEncodeRre) {
            profHosts[id].encode = rfbEncodingRRE ;
        }
        if (argEncodeCor) {
            profHosts[id].encode = rfbEncodingCoRRE ;
        }
        if (argEncodeHex) {
            profHosts[id].encode = rfbEncodingHextile ;
        }
        if (argOptShared) {
            profHosts[id].shared = TRUE ;
        }
        if (argOptViewonly) {
            profHosts[id].viewonly = TRUE ;
        }
        if (argOptDeiconify) {
            profHosts[id].deiconify = TRUE ;
        }
    }

    /*
     * also modify current session parameters
     */

    if (argServer) {
        strcpy(SessServerName, argServer) ;
    }
    if (argFormat32) {
        SessPixelFormat = PIXFMT_32 ;
    }
    if (argFormat8) {
        SessPixelFormat = PIXFMT_8 ;
    }
    if (argFormatTiny) {
        SessPixelFormat = PIXFMT_TINY ;
    }
    if (argFormatGray) {
        SessPixelFormat = PIXFMT_GRAY ;
    }
    if (argEncodeRaw) {
        SessPreferredEncoding = rfbEncodingRaw ;
    }
    if (argEncodeRre) {
        SessPreferredEncoding = rfbEncodingRRE ;
    }
    if (argEncodeCor) {
        SessPreferredEncoding = rfbEncodingCoRRE ;
    }
    if (argEncodeHex) {
        SessPreferredEncoding = rfbEncodingHextile ;
    }
    if (argOptShared) {
        SessOptShared = TRUE ;
    }
    if (argOptViewonly) {
        SessOptViewonly = TRUE ;
    }
    if (argOptDeiconify) {
        SessOptDeiconify = TRUE ;
    }

#ifdef DEBUG
    TRACE("loadProfile ... done\n") ;
#endif

    return TRUE ;
}

/*
 * loadParam - set paramaters for given server
 */

static  int     loadParam(PUCHAR server)
{
    int     i, id ;

#ifdef DEBUG
    TRACE("loadParam for server %s\n", server) ;
#endif

    for (i = 0 ; i < MAXHOSTS ; i++) {
        if ((id = profOrder[i]) == 0xff) {
            continue ;
        }
        if (stricmp(server, profHosts[id].name) != 0) {
            continue ;
        }
        strcpy(SessServerName, server) ;
        SessPixelFormat       = profHosts[id].format    ;
        SessPreferredEncoding = profHosts[id].encode    ;
        SessOptShared         = profHosts[id].shared    ;
        SessOptViewonly       = profHosts[id].viewonly  ;
        SessOptDeiconify      = profHosts[id].deiconify ;
        SessSavedRfb          = profHosts[id].rfb       ;
        SessSavedPos          = profHosts[id].pos       ;
        SessSavedSiz          = profHosts[id].siz       ;
        SessSavedOff          = profHosts[id].off       ;

#ifdef DEBUG
        TRACE("loadParam ... loaded %d data to host %s\n", id, server) ;
        TRACE("  rfb %dx%d, pos %d,%d, siz %dx%d, off %d,%d\n",
                        SessSavedRfb.cx, SessSavedRfb.cy,
                        SessSavedPos.x,  SessSavedPos.y,
                        SessSavedSiz.cx, SessSavedSiz.cy,
                        SessSavedOff.x,  SessSavedOff.y) ;
#endif

        return id ;
    }
    strcpy(SessServerName, server) ;

#ifdef DEBUG
    TRACE("loadParam ... host %s use current values\n", server) ;
#endif

    return -1 ;
}

/*
 * saveParam - save parameter with host 'SessServerName'
 */

static  int     saveParam(void)
{
    int     i, j, id, entry ;
    UCHAR   newOrder[MAXHOSTS + 1] ;

#ifdef DEBUG
    TRACE("in saveParam for <%s>\n", SessServerName) ;
#endif

    if (strlen(SessServerName) == 0) {
        return -1 ;
    }
    if (strchr(SessServerName, ':') == NULL) {
        return -1 ;
    }

    /*
     * select HOST entry for 'SessServerName'
     */

    entry = -1 ;

    if (entry == -1) {          /* already exist in list */
        for (i = 0 ; i < MAXHOSTS ; i++) {
            if (stricmp(profHosts[i].name, SessServerName) == 0) {
                entry = i ;

#ifdef DEBUG
                TRACE("saveParam - use existing entry %d\n", entry) ;
#endif

                break ;
            }
        }

    }
    if (entry == -1) {          /* use empty entry */
        for (i = 0 ; i < MAXHOSTS ; i++) {
            if (profOrder[i] == 0xff) {
                entry = i ;

#ifdef DEBUG
                TRACE("saveParam - use empty entry %d\n", entry) ;
#endif

                break ;
            }
        }
    }
    if (entry == -1) {          /* use last one */
        entry = profOrder[MAXHOSTS - 1] ;

#ifdef DEBUG
        TRACE("saveParam - replace %d\n", entry) ;
#endif

    }

    memset(newOrder, 0xff, sizeof(newOrder)) ;
    newOrder[0] = entry ;

    for (i = 0, j = 1 ; i < MAXHOSTS ; i++) {
        if ((id = profOrder[i]) == 0xff) {
            continue ;
        }
        if (id == entry) {
            continue ;
        }
        newOrder[j++] = id ;
    }

    memcpy(profOrder, newOrder, sizeof(profOrder)) ;

    /*
     * save to host entry
     */

    strcpy(profHosts[entry].name, SessServerName)      ;
    profHosts[entry].format    = SessPixelFormat       ;
    profHosts[entry].encode    = SessPreferredEncoding ;
    profHosts[entry].shared    = SessOptShared         ;
    profHosts[entry].viewonly  = SessOptViewonly       ;
    profHosts[entry].deiconify = SessOptDeiconify      ;
    profHosts[entry].rfb       = SessSavedRfb          ;
    profHosts[entry].pos       = SessSavedPos          ;
    profHosts[entry].siz       = SessSavedSiz          ;
    profHosts[entry].off       = SessSavedOff          ;

#ifdef DEBUG
    TRACE("saveParam ... saved to %d entry\n", entry) ;
    TRACE("  rfb %dx%d, pos %d,%d, siz %dx%d, off %d,%d\n",
                SessSavedRfb.cx, SessSavedRfb.cy,
                SessSavedPos.x,  SessSavedPos.y,
                SessSavedSiz.cx, SessSavedSiz.cy,
                SessSavedOff.x,  SessSavedOff.y) ;
    TRACE("done saveParam\n");
#endif

    return entry ;
}

/*
 * saveProfile - save current parameter as profile data
 */

static  BOOL    saveProfile(HAB hab)
{
#ifdef DEBUG
    TRACE("in saveProfile ... ") ;
#endif

    int     entry, i, id ;
    HINI    hini  ;
    BOOL    stat  ;
    ULONG   len   ;
    UCHAR   key[32] ;

#ifdef DEBUG
    TRACE("done in saveProfile\n") ;
#endif

    /*
     * save to profile
     */

    if ((hini = PrfOpenProfile(hab, ProfilePath)) == NULLHANDLE) {

#ifdef DEBUG
        TRACE("saveProfile - failed to open %s\n", ProfilePath) ;
#endif

        return FALSE ;
    }

    /*
     * re-load new profile
     */

    len = sizeof(profOrder) ;
    stat = PrfQueryProfileData(hini, ProgramName, "ORDER", profOrder, &len) ;

    if (stat != TRUE || len != sizeof(profOrder)) {
        memset(profOrder, 0xff, sizeof(profOrder)) ;
    }

    for (i = 0 ; i < MAXHOSTS ; i++) {
        if ((id = profOrder[i]) == 0xff) {
            continue ;
        }
        sprintf(key, "HOST%02d", id) ;
        len = sizeof(HOSTREC) ;
        stat = PrfQueryProfileData(hini,
                    ProgramName, key, &profHosts[id], &len) ;
        if (stat != TRUE || len != sizeof(HOSTREC)) {
            profOrder[i] = 0xff ;
        }
    }

    /*
     * select new entry
     */

    if ((entry = saveParam()) < 0) {
        PrfCloseProfile(hini) ;
        return FALSE ;
    }

    /*
     * save new order and host data
     */

    sprintf(key, "HOST%02d", entry) ;
    PrfWriteProfileData(hini, ProgramName,
                "ORDER", profOrder, sizeof(profOrder)) ;
    PrfWriteProfileData(hini, ProgramName,
                key, &profHosts[entry], sizeof(HOSTREC)) ;

    PrfCloseProfile(hini) ;

#ifdef DEBUG
    TRACE("saveProfile ... saved to %d\n", entry) ;
#endif

    return TRUE ;
}

/*
 * set/get Button State
 */

static  void    setButton(HWND hwndDlg, USHORT id, BOOL state)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, id) ;

    WinSendMsg(hwnd, BM_SETCHECK, MPFROMSHORT(state), NULL) ;
}

static  BOOL    getButton(HWND hwndDlg, USHORT id)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, id) ;
    BOOL    state ;

    state = (BOOL) WinSendMsg(hwnd, BM_QUERYCHECK, NULL, NULL) ;
    return state ;
}

/*
 * set/get Pixel Format
 */

typedef struct _FORMAT {
    PUCHAR  str ;
    SHORT   fmt ;
} FMTREC, *FMTPTR ;

static  FMTREC  tabFormat[] = {
    { "True Color", PIXFMT_32   } ,
    { "BGR233",     PIXFMT_8    } ,
    { "Tiny Color", PIXFMT_TINY } ,
    { "Gray Scale", PIXFMT_GRAY } ,
    { NULL,     0         }
} ;

static  void    setFormat(HWND hwndDlg)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_OPT_FORMAT) ;
    FMTPTR  p ;

    for (p = tabFormat ; p->str != NULL ; p++) {
        WinSendMsg(hwnd, LM_INSERTITEM,
            MPFROM2SHORT(LIT_END, 0), MPFROMP(p->str)) ;
    }
    for (p = tabFormat ; p->str != NULL ; p++) {
        if (p->fmt == SessPixelFormat) {
            WinSetWindowText(hwnd, p->str) ;
            return ;
        }
    }
    WinSetWindowText(hwnd, tabFormat[0].str) ;
}

static  void    getFormat(HWND hwndDlg)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_OPT_FORMAT) ;
    UCHAR   name[64] ;
    FMTPTR  p ;

    WinQueryWindowText(hwnd, 64, name) ;

    for (p = tabFormat ; p->str != NULL ; p++) {
        if (strcmp(name, p->str) == 0) {
            SessPixelFormat = p->fmt ;
            return ;
        }
    }
}

/*
 * set/get Preferred Encoding
 */

typedef struct _ENCODE {
    PUCHAR  str ;
    CARD32  enc ;
} ENCREC, *ENCPTR ;

static  ENCREC  tabEncode[] = {
    { "Hextile", rfbEncodingHextile } ,
    { "RRE",     rfbEncodingRRE     } ,
    { "CoRRE",   rfbEncodingCoRRE   } ,
    { "Raw",     rfbEncodingRaw     } ,
    { NULL,      0                  }
} ;

static  void    setEncode(HWND hwndDlg)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_OPT_ENCODE) ;
    ENCPTR  p ;

    for (p = tabEncode ; p->str != NULL ; p++) {
        WinSendMsg(hwnd, LM_INSERTITEM,
            MPFROM2SHORT(LIT_END, 0), MPFROMP(p->str)) ;
    }
    for (p = tabEncode ; p->str != NULL ; p++) {
        if (p->enc == SessPreferredEncoding) {
            WinSetWindowText(hwnd, p->str) ;
            return ;
        }
    }
    WinSetWindowText(hwnd, tabEncode[0].str) ;
}

static  void    getEncode(HWND hwndDlg)
{
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_OPT_ENCODE) ;
    UCHAR   name[64] ;
    ENCPTR  p ;

    WinQueryWindowText(hwnd, 64, name) ;

    for (p = tabEncode ; p->str != NULL ; p++) {
        if (strcmp(name, p->str) == 0) {
            SessPreferredEncoding = p->enc ;
            return ;
        }
    }
}

/*
 * set/get Session Options
 */

static  BOOL    initialSetup ;

static  void    setOptions(HWND hwndDlg)
{
    UCHAR   title[256] ;

    sprintf(title, "Session Options for <%s>", SessServerName) ;
    WinSetWindowText(hwndDlg, title) ;

    setFormat(hwndDlg) ;
    setEncode(hwndDlg) ;

    setButton(hwndDlg, IDD_OPT_SHARED, SessOptShared)    ;
    setButton(hwndDlg, IDD_OPT_VONLY,  SessOptViewonly)  ;
    setButton(hwndDlg, IDD_OPT_DEICON, SessOptDeiconify) ;

    if (! initialSetup) {
        WinEnableWindow(WinWindowFromID(hwndDlg, IDD_OPT_FORMAT), FALSE) ;
        WinEnableWindow(WinWindowFromID(hwndDlg, IDD_OPT_SHARED), FALSE) ;
    }
}

static  BOOL    getOptions(HWND hwndDlg)
{
    getFormat(hwndDlg) ;
    getEncode(hwndDlg) ;

    SessOptShared    = getButton(hwndDlg, IDD_OPT_SHARED) ;
    SessOptViewonly  = getButton(hwndDlg, IDD_OPT_VONLY) ;
    SessOptDeiconify = getButton(hwndDlg, IDD_OPT_DEICON) ;

    return TRUE ;
}

/*
 * procOpts - dialog proc. for Session Options
 */

static MRESULT EXPENTRY procOpts(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    ULONG   ret ;

    switch (msg) {

    case WM_INITDLG :
        if (SessDlgCenter) {
            dialogAtCenter(hwnd) ;
        } else {
            dialogAtMouse(hwnd, DID_OK) ;
        }
        setOptions(hwnd)   ;
        return (MRESULT) 0 ;

    case WM_COMMAND :
        switch (ret = SHORT1FROMMP(mp1)) {
        case DID_OK :
        case DID_CANCEL :
            if (getOptions(hwnd) != TRUE) {
                return (MRESULT) 0 ;
            }
            saveParam() ;
            WinDismissDlg(hwnd, ret) ;
            return (MRESULT) 0 ;
        default :
            return (MRESULT) 0 ;
        }
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2) ;
}

/*
 * set/getServer - set/get Server Spec.
 */

static  void    setServer(HWND hwndDlg)
{
//    HAB     hab = WinQueryAnchorBlock(hwndDlg) ;
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_SHOST) ;
    int     i, id ;
    PUCHAR  first = NULL ;
    UCHAR   server[128] ;

    WinSendMsg(hwnd, EM_SETTEXTLIMIT, MPFROMSHORT(128), NULL) ;

    /*
     * fill-in host list
     */

    for (i = 0 ; i< MAXHOSTS ; i++) {
        if ((id = profOrder[i]) == 0xff) {
            continue ;
        }

#ifdef DEBUG
        TRACE("setServer - %d : %s\n", id, profHosts[id].name) ;
#endif

        if (first == NULL) {
            first = profHosts[id].name ;
        }
        WinSendMsg(hwnd, LM_INSERTITEM,
            MPFROM2SHORT(LIT_END, 0), MPFROMP(profHosts[id].name)) ;
    }

    /*
     * set host name (current selection)
     */

    if (argServer != NULL) {
        strcpy(server, argServer) ;
    } else if (first != NULL) {
        strcpy(server, first) ;
    } else {
        strcpy(server, "") ;
    }

#ifdef DEBUG
    TRACE("setServer - host %s\n", server) ;
#endif

    WinSetWindowText(hwnd, server) ;
}

static  BOOL    getServer(HWND hwndDlg)
{
//    HAB     hab  = WinQueryAnchorBlock(hwndDlg) ;
    HWND    hwnd = WinWindowFromID(hwndDlg, IDD_SHOST) ;
    UCHAR   server[128] ;

    WinQueryWindowText(hwnd, 128, server) ;

    if (strlen(server) == 0) {
        return FALSE ;
    }
    if (strchr(server, ':') == NULL) {
        return FALSE ;
    }
    loadParam(server) ;

    return TRUE ;
}

/*
 * procSess - dialog proc. for Session Setup
 */

static MRESULT EXPENTRY procSess(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    ULONG   ret ;

    switch (msg) {

    case WM_INITDLG :
        if (SessDlgCenter) {
            dialogAtCenter(hwnd) ;
        } else {
            dialogAtMouse(hwnd, DID_OK) ;
        }
        setServer(hwnd)    ;
        return (MRESULT) 0 ;

    case WM_COMMAND :
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK :
            if (getServer(hwnd) != TRUE) {
                return (MRESULT) 0 ;
            }
            WinDismissDlg(hwnd, DID_OK) ;
            return (MRESULT) 0 ;
        case DID_CANCEL :
            WinDismissDlg(hwnd, DID_CANCEL) ;
            return (MRESULT) 0 ;
        case IDD_SOPTS :
            getServer(hwnd) ;
            ret = WinDlgBox(HWND_DESKTOP, hwndClient,
                        procOpts, NULLHANDLE, IDD_OPTS_INI, NULL) ;
            if (ret == DID_OK && getServer(hwnd) == TRUE) {
                WinDismissDlg(hwnd, DID_OK) ;
            }
            return (MRESULT) 0 ;
        default :
            return (MRESULT) 0 ;
        }
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2) ;
}

/*
 * sessSetup - Session Setup, entry
 */

BOOL    sessSetup(HAB hab)
{
    ULONG   ret ;

    initialSetup = TRUE ;

    loadProfile(hab) ;

    ret = WinDlgBox(HWND_DESKTOP, HWND_DESKTOP,
                procSess, NULLHANDLE, IDD_SESS, NULL) ;

    if (ret == DID_OK) {
        saveProfile(hab) ;
    }
    return (ret == DID_OK) ? TRUE : FALSE ;
}

/*
 * sessModify - Change Session option during a session
 */

BOOL    sessModify(HAB hab)
{
    ULONG   ret ;
    CARD32  lastEnc ;

    initialSetup = FALSE ;
    lastEnc = SessPreferredEncoding ;

    ret = WinDlgBox(HWND_DESKTOP, HWND_DESKTOP,
                procOpts, NULLHANDLE, IDD_OPTS_MOD, NULL) ;
    if (ret != DID_OK) {
        return FALSE ;
    }

    if (lastEnc != SessPreferredEncoding) {
        protoSendFmtEnc() ;
    }
    return TRUE ;
}

/*
 * sessSaveProfile - external entry to save current profile
 */

BOOL    sessSaveProfile(HAB hab)
{
    return saveProfile(hab) ;
}
