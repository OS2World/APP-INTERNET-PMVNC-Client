/*
 * proto.c - PM VNC Viewer, RFB Protocol Handling
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"
#include "vncauth.h"

/*
 * VNC Session Context - depends on Pixel Format
 */

VNCPTR  VncCtx = &VncCtx32 ;

UCHAR   VncGreeting[] = "Please wait - initial screen loading" ;

/*
 * disable input till session ready
 */

static  BOOL    inputReady = FALSE ;

/*
 * chkVersion - Exchange Protocol Version
 */

static  BOOL    authWillWork = TRUE ;
static  int     verMajor, verMinor  ;

static  BOOL    chkVersion(void)
{
    rfbProtocolVersionMsg   pv ;
    int     major, minor ;

    if (netRecv(pv, sz_rfbProtocolVersionMsg) != TRUE) {
        netFail("failed to recv RFB Version") ;
        return FALSE ;
    }
    pv[sz_rfbProtocolVersionMsg] = '\0' ;

    if (sscanf(pv, rfbProtocolVersionFormat, &major, &minor) != 2) {
        netFail("failed to get Protocol Version") ;
        return FALSE ;
    }

#ifdef DEBUG
    TRACE("Server RFB Protocol Version %d.%d\n", major, minor) ;
#endif

    if (major != rfbProtocolMajorVersion) {
        netFail("RFB Major Version Unmatch") ;
        return FALSE ;
    }
    if (minor < rfbProtocolMinorVersion) {
        authWillWork = FALSE ;
    } else {
        major = rfbProtocolMajorVersion ;
        minor = rfbProtocolMinorVersion ;
    }
    sprintf(pv, rfbProtocolVersionFormat, major, minor) ;

    if (netSend(pv, sz_rfbProtocolVersionMsg) != TRUE) {
        netFail("failed to send Protocol Version") ;
        return FALSE ;
    }
    verMajor = major ;
    verMinor = minor ;

    return TRUE ;
}

/*
 * chkAuth - Authentication
 */

static  UCHAR   authChallenge[CHALLENGESIZE] ;

static  BOOL    chkAuth(void)
{
    CARD32  authScheme, reasonLen, authResult ;
    PUCHAR  reason ;

    /*
     * get auth. scheme
     */

    if (netRecv((PUCHAR) &authScheme, sizeof(CARD32)) != TRUE) {
        netFail("failed to recv Auth. Scheme") ;
        return FALSE ;
    }
    authScheme = swap32(authScheme) ;


#ifdef DEBUG
    TRACE("Auth. Scheme %d\n", authScheme) ;
#endif

    if (authScheme == rfbNoAuth) {

#ifdef DEBUG
        TRACE("no authentication needed\n") ;
#endif

        return TRUE ;
    }
    if (authScheme == rfbConnFailed) {
        if (netRecv((PUCHAR) &reasonLen, sizeof(CARD32)) != TRUE) {
            netFail("failed to recv reason length") ;
            return FALSE ;
        }
        reasonLen = swap32(reasonLen) ;

        if ((reason = malloc(reasonLen)) == NULL) {
            netFail("failed to alloc reason buffer") ;
            return FALSE ;
        }
        if (netRecv(reason, reasonLen) != TRUE) {
            netFail("failed to recv reason") ;
            return FALSE ;
        }
        netFail(reason) ;       /* don't free 'reason'  */
        return FALSE ;
    }
    if (authScheme != rfbVncAuth) {
        netFail("unknown authenticaion scheme") ;
        return FALSE ;
    }

    /*
     * case of rfbVncAuth
     */

    if (! authWillWork) {
        netFail("VNC Server uses old Auth. scheme") ;
        return FALSE ;
    }
    if (netRecv(authChallenge, CHALLENGESIZE) != TRUE) {
        netFail("failed to recv auth. challenge") ;
        return FALSE ;
    }
    netNotify(WM_VNC_AUTH, NULL, MPFROMP(authChallenge)) ;
        /*
         * then window thread encode 'authChallenge'
         * and calls protoSendAuth
         */
    if (netRecv((PUCHAR) &authResult, sizeof(CARD32)) != TRUE) {
        netFail("failed to recv auth. result") ;
        return FALSE ;
    }
    authResult = swap32(authResult) ;

#ifdef DEBUG
    TRACE("auth. result %d\n", authResult) ;
#endif

    switch (authResult) {
    case rfbVncAuthOK :

#ifdef DEBUG
        TRACE("rfbVncAuthOK\n") ;
#endif

        return TRUE ;
    case rfbVncAuthFailed :
        netFail("rfbVncAuthFailed") ;
        return FALSE ;
    case rfbVncAuthTooMany :
        netFail("rfbVncAuthTooMany") ;
        return FALSE ;
    default :
        netFail("unknown VNC auth. result") ;
        return FALSE ;
    }
    return FALSE ;      /* not reach here   */
}

/*
 * protoSendAuth - Send Authentication Data
 */

BOOL    protoSendAuth(void)
{
    if (netSend(authChallenge, CHALLENGESIZE) != TRUE) {
        netFail("failed to Send Auth.") ;
        return FALSE ;
    }
    return TRUE ;
}

/*
 * chkInit - Client/Server Initialization Message
 */

static  rfbServerInitMsg    ServerInit ;
static  PUCHAR              ServerDesk ;

static  BOOL    chkInit(void)
{
    rfbClientInitMsg    ci ;
    rfbServerInitMsg    si ;
    int     cx, cy ;

    /*
     * send Client Initialization Message
     */

    ci.shared = (SessOptShared ? 1 : 0) ;

    if (netSend((PUCHAR) &ci, sz_rfbClientInitMsg) != TRUE) {
        netFail("failed to send ClientInit") ;
        return FALSE ;
    }

    /*
     * recv Server Initialization Message
     */

    if (netRecv((PUCHAR) &si, sz_rfbServerInitMsg) != TRUE) {
        netFail("failed to recv ServerInit") ;
        return FALSE ;
    }

    si.framebufferWidth  = swap16(si.framebufferWidth)  ;
    si.framebufferHeight = swap16(si.framebufferHeight) ;
    si.format.redMax   = swap16(si.format.redMax)   ;
    si.format.greenMax = swap16(si.format.greenMax) ;
    si.format.blueMax  = swap16(si.format.blueMax)  ;
    si.nameLength = swap32(si.nameLength) ;

    memcpy(&ServerInit, &si, sizeof(rfbServerInitMsg)) ;

    if ((ServerDesk = malloc(si.nameLength + 2)) == NULL) {
        netFail("failed to alloc desktop name") ;
        return FALSE ;
    }
    memset(ServerDesk, 0, si.nameLength + 2) ;

    if (netRecv(ServerDesk, si.nameLength) != TRUE) {
        netFail("failed to recv desktop name") ;
        return FALSE ;
    }

    cx = si.framebufferWidth  ;
    cy = si.framebufferHeight ;

#if 1
    printf("Remote Desktop [%s] %d x %d\n", ServerDesk, cx, cy) ;
    printf("Server's natural format is\n") ;
    printf("    bitsPerPixel %d, depth %d, bigEndian %s, trueColour %s\n",
                si.format.bitsPerPixel, si.format.depth,
                (si.format.bigEndian ? "TRUE" : "FALSE"),
                (si.format.trueColour ? "TRUE" : "FALSE")) ;
    printf("    red   Max %d, shift %d\n",
                si.format.redMax,   si.format.redShift) ;
    printf("    green Max %d, shift %d\n",
                si.format.greenMax, si.format.greenShift) ;
    printf("    blue  Max %d, shift %d\n",
                si.format.blueMax,  si.format.blueShift) ;
#endif

    if ((*VncCtx->rectInit) (cx, cy) != TRUE) {
        netFail("failed to initialize frame buffer") ;
        return FALSE ;
    }

    netNotify(WM_VNC_INIT, MPFROM2SHORT(cx, cy), MPFROMP(ServerDesk)) ;

    return TRUE ;
}

/*
 * protoConnInit - Initialize RFB Connection
 */

BOOL    protoConnInit(void)
{
    int     nColors ;

    nColors = queryColorIndex() ;

    /*
     * Select Protocol Context
     */

    switch (SessPixelFormat) {
    case PIXFMT_32 :
        VncCtx = &VncCtx32 ;
        break ;
    case PIXFMT_8  :
        VncCtx = (nColors >= 256) ? &VncCtx8 : &VncCtx4Tiny ;
        break ;
    case PIXFMT_TINY  :
        VncCtx = &VncCtx4Tiny ;
        break ;
    case PIXFMT_GRAY  :
        VncCtx = &VncCtx4Gray ;
        break ;
    default        :
        VncCtx = &VncCtx32 ;
        break ;
    }

    if (chkVersion() != TRUE) {
        return FALSE ;
    }
    if (chkAuth() != TRUE) {
        return FALSE ;
    }
    if (chkInit() != TRUE) {
        return FALSE ;
    }
    return TRUE ;
}

/*
 * protoConnInfo - query connection info.
 */

PUCHAR  protoConnInfo(rfbServerInitMsg *si, int *major, int *minor)
{
    *si = ServerInit  ;
    *major = verMajor ;
    *minor = verMinor ;
    return ServerDesk ;
}

/*
 * protoSendFmtEnc - send SetPixelFormat & SetEncodings
 */

#define MAX_ENCODINGS   10

static  CARD32  DefaultEncodingsTab[] = {
    rfbEncodingHextile,
    rfbEncodingCoRRE,
    rfbEncodingRRE,
    rfbEncodingRaw                  /* at last */
} ;

#define     DefaultEncodingsNum (sizeof(DefaultEncodingsTab)/sizeof(CARD32))

BOOL    protoSendFmtEnc(void)
{
    rfbSetPixelFormatMsg    spf ;
    rfbSetEncodingsMsg      *se ;
    CARD32  *encs  ;
    CARD16  num    ;
    int     i, len ;
    UCHAR   buf[sz_rfbSetEncodingsMsg + sizeof(CARD32) * MAX_ENCODINGS] ;

    /*
     * send SetPixelFormat Message
     */

    spf.type = rfbSetPixelFormat ;
    spf.format = VncCtx->rectFormat ;

    spf.format.redMax   = swap16(spf.format.redMax)   ;
    spf.format.greenMax = swap16(spf.format.greenMax) ;
    spf.format.blueMax  = swap16(spf.format.blueMax)  ;

    if (netSend((PUCHAR) &spf, sz_rfbSetPixelFormatMsg) != TRUE) {
        netFail("failed to send SetPixelFormat") ;
        return FALSE ;
    }

    /*
     * send SetEncodings Message
     */

    se   = (rfbSetEncodingsMsg *) &buf[0] ;
    encs = (CARD32 *) &buf[sz_rfbSetEncodingsMsg] ;

    se->type = rfbSetEncodings ;
    len = sz_rfbSetEncodingsMsg ;
    num = 0 ;

#ifdef DEBUG
    TRACE("Pref Encoding %d\n", SessPreferredEncoding) ;
#endif

    *encs++ = swap32(rfbEncodingCopyRect)  ;    /* always on top    */
    num += 1 ;
    len += sizeof(CARD32)  ;

    *encs++ = swap32(SessPreferredEncoding) ;   /* then preferred one   */
    num += 1 ;
    len += sizeof(CARD32)  ;

    for (i = 0 ; i < DefaultEncodingsNum ; i++) {   /* as default   */
        *encs++ = swap32(DefaultEncodingsTab[i]) ;
        num += 1 ;
        len += sizeof(CARD32)  ;
    }
    se->nEncodings = swap16(num) ;

    if (netSend(buf, len) != TRUE) {
        netFail("failed to send SetEncodings") ;
        return FALSE ;
    }

    inputReady = TRUE ;

    return TRUE ;
}

/*
 * protoSendRequest - send FramebufferUpdateRequest
 */

BOOL    protoSendRequest(BOOL inc, PRECTL rect)
{
    rfbFramebufferUpdateRequestMsg  fur ;
    RECTL   full ;

    fur.type = rfbFramebufferUpdateRequest ;
    fur.incremental = inc ? 1 : 0 ;

    if (rect == NULL) {
        full.xLeft   = 0 ;
        full.yBottom = 0 ;
        full.xRight  = ServerInit.framebufferWidth  ;
        full.yTop    = ServerInit.framebufferHeight ;
        rect = &full ;
    }
    fur.x = swap16(rect->xLeft) ;
    fur.w = swap16(rect->xRight - rect->xLeft) ;
    fur.y = swap16(ServerInit.framebufferHeight - rect->yTop) ;
    fur.h = swap16(rect->yTop - rect->yBottom) ;

    if (netSend((PUCHAR) &fur, sz_rfbFramebufferUpdateRequestMsg) != TRUE) {
        netFail("failed to send FramebufferUpdateRequest") ;
        return FALSE ;
    }
    return TRUE ;
}

/*
 * protoSetColourMapEntries
 *      not expected for TRUE COLOR, but read and skip message
 */

static  BOOL    protoSetColourMapEntries(rfbServerToClientMsg *msg)
{
    int     i ;
    CARD16  rgb[3] ;

#ifdef DEBUG
    TRACE("rfbSetColourMapEntries - unexpected, skip\n") ;
#endif

    if (netRecv(((PUCHAR) msg) + 1, sz_rfbSetColourMapEntriesMsg - 1) != TRUE) {
        netFail("failed to recv rfbSetColourMapEntires") ;
        return FALSE ;
    }

    msg->scme.firstColour = swap16(msg->scme.firstColour) ;
    msg->scme.nColours    = swap16(msg->scme.nColours)    ;

    for (i = 0 ; i < msg->scme.nColours ; i++) {
        if (netRecv((PUCHAR) rgb, sizeof(rgb)) != TRUE) {
            netFail("failed to recv rfbSetColourMapEntires, RGB") ;
            return FALSE ;
        }
    }
    return TRUE ;
}

/*
 * protoBell - notify to window thread to ring a bell
 */

static  BOOL    protoBell(rfbServerToClientMsg *msg)
{

#ifdef DEBUG
    TRACE("rfbBell\n") ;
#endif

    netNotify(WM_VNC_BELL, NULL, NULL) ;
    return TRUE ;
}

/*
 * protoServerCutText - read cut text and notify to window
 */

static  BOOL    protoServerCutText(rfbServerToClientMsg *msg)
{
    PUCHAR  str ;

#ifdef DEBUG
    TRACE("rfbServerCutText\n") ;
#endif

    if (netRecv(((PUCHAR) msg) + 1, sz_rfbServerCutTextMsg - 1) != TRUE) {
        netFail("failed to recv rfbServerCutTextMsg") ;
        return FALSE ;
    }

    msg->sct.length = swap32(msg->sct.length) ;

    if ((str = malloc(msg->sct.length + 2)) == NULL) {
        netFail("failed to alloc for rfbServerCutText") ;
        return FALSE ;
    }

    if (netRecv(str, msg->sct.length) != TRUE) {
        netFail("failed to recv rfbServerCutText, text") ;
        return FALSE ;
    }
    str[msg->sct.length] = '\0' ;

    netNotify(WM_VNC_CLIP, NULL, MPFROMP(str)) ;    /* disposed in window */

    return TRUE ;
}

/*
 * protoFramebufferUpdate
 */

static  int     xmin, xmax, ymin, ymax ;

#define regnInit()              \
    {                           \
        xmax = ymax = 0 ;       \
        xmin = ymin = 0xffff ;  \
    }
#define regnJoin(x, y, w, h)                                \
    {                                                       \
        xmin = ((x) < xmin) ? (x) : xmin ;                  \
        ymin = ((y) < ymin) ? (y) : ymin ;                  \
        xmax = (((x) + (w)) > xmax) ? ((x) + (w)) : xmax ;  \
        ymax = (((y) + (h)) > ymax) ? ((y) + (h)) : ymax ;  \
    }
#define regnUpdate()                                                    \
    {                                                                   \
        netNotify(WM_VNC_UPDATE,                                        \
            MPFROM2SHORT(xmin, ServerInit.framebufferHeight - ymax),    \
            MPFROM2SHORT((xmax - xmin), (ymax - ymin))) ;               \
    }

static  BOOL    protoFramebufferUpdate(rfbServerToClientMsg *msg)
{
    rfbFramebufferUpdateRectHeader  rect ;
    int     i    ;
    BOOL    stat ;
    int     x, y, w, h ;

    if (netRecv(((PUCHAR) msg) + 1, sz_rfbFramebufferUpdateMsg - 1) != TRUE) {
        netFail("failed to recv rfbFramebufferUpdateMsg") ;
        return FALSE ;
    }
    msg->fu.nRects = swap16(msg->fu.nRects) ;

    regnInit() ;

    for (i = 0 ; i < msg->fu.nRects ; i++) {

        if (netRecv((PUCHAR) &rect, sz_rfbFramebufferUpdateRectHeader) != TRUE) {
            netFail("failed to recv rfbFramebufferUpdateRectHeader") ;
            return FALSE ;
        }
        rect.r.x = x = swap16(rect.r.x) ;
        rect.r.y = y = swap16(rect.r.y) ;
        rect.r.w = w = swap16(rect.r.w) ;
        rect.r.h = h = swap16(rect.r.h) ;
        rect.encoding = swap32(rect.encoding) ;

        if ((rect.r.x + rect.r.w) > ServerInit.framebufferWidth) {
            netFail("rect too large (width)") ;
            netDump((PUCHAR) &rect, sz_rfbFramebufferUpdateRectHeader) ;
            return FALSE ;
        }
        if ((rect.r.y + rect.r.h) > ServerInit.framebufferHeight) {
            netFail("rect too large (height)") ;
            netDump((PUCHAR) &rect, sz_rfbFramebufferUpdateRectHeader) ;
            return FALSE ;
        }
        if (rect.r.w == 0 || rect.r.h == 0) {

#ifdef DEBUG
            TRACE("empty rectangle, skip\n") ;
#endif

            continue ;
        }

        switch (rect.encoding) {
        case rfbEncodingRaw      :
            stat = (*VncCtx->rectRaw) (&rect.r) ;
            break ;
        case rfbEncodingCopyRect :
            stat = (*VncCtx->rectCopy) (&rect.r) ;
            break ;
        case rfbEncodingRRE      :
            stat = (*VncCtx->rectRRE) (&rect.r) ;
            break ;
        case rfbEncodingCoRRE    :
            stat = (*VncCtx->rectCoRRE) (&rect.r) ;
            break ;
        case rfbEncodingHextile  :
            stat = (*VncCtx->rectTile) (&rect.r) ;
            break ;
        default :
            netFail("unknown rect encoding") ;
            stat = FALSE ;
            break ;
        }
        if (stat != TRUE) {
            return FALSE ;
        }
        regnJoin(x, y, w, h) ;
    }
    regnUpdate() ;

    if (winIconized() == FALSE) {
        protoSendRequest(TRUE, NULL) ;
    }
    return TRUE ;
}

/*
 * protoDispatch - entry for Server -> Client Message Handling
 */

BOOL    protoDispatch(void)
{
    rfbServerToClientMsg    msg ;

    if (netRecv((PUCHAR) &msg, 1) != TRUE) {
        netFail("failed to recv Server -> Client Msg") ;
        return FALSE ;
    }

    switch (msg.type) {
    case rfbSetColourMapEntries :
        return protoSetColourMapEntries(&msg) ;
    case rfbFramebufferUpdate :
        return protoFramebufferUpdate(&msg) ;
    case rfbBell :
        return protoBell(&msg) ;
    case rfbServerCutText :
        return protoServerCutText(&msg) ;
    }
    netFail("unknown message from Server") ;
    return FALSE ;
}

/*
 * protoSendMouEvent - send Pointer Event
 */

BOOL    protoSendMouEvent(USHORT state, PPOINTL pt)
{
    rfbPointerEventMsg  pe ;

    if (SessOptViewonly) {
        return TRUE ;
    }
    if (! inputReady) {
        return TRUE ;
    }

    pe.type = rfbPointerEvent ;
    pe.buttonMask = (UCHAR) (state & 0xff) ;
    pe.x = swap16((USHORT) pt->x) ;
    pe.y = swap16((USHORT) pt->y) ;

    return netSend((PUCHAR) &pe, sz_rfbPointerEventMsg) ;
}

/*
 * protoSendKeyEvent - send Keyboard Event
 */

BOOL    protoSendKeyEvent(BOOL down, ULONG key)
{
    rfbKeyEventMsg  ke ;

    if (SessOptViewonly) {
        return TRUE ;
    }
    if (! inputReady) {
        return TRUE ;
    }

    ke.type = rfbKeyEvent  ;
    ke.down = down ? 1 : 0 ;
    ke.key  = swap32(key)  ;

    return netSend((PUCHAR) &ke, sz_rfbKeyEventMsg) ;
}

/*
 * protoSendCutText - send Client Cut Text
 */

BOOL    protoSendCutText(PUCHAR text)
{
    rfbClientCutTextMsg *cct ;
    int     len     ;
    PUCHAR  buf, dp ;
    BOOL    stat    ;

    if ((buf = malloc(sz_rfbClientCutTextMsg + strlen(text))) == NULL) {
        return FALSE ;
    }
    cct = (rfbClientCutTextMsg *) buf  ;
    dp  = buf + sz_rfbClientCutTextMsg ;

    for (len = 0 ; *text != '\0' ; len++, text++) {
        if (*text == '\r') {
            continue ;      /* skip CR */
        }
        dp[len] = *text ;
    }

    cct->type   = rfbClientCutText ;
    cct->length = swap32(len)      ;

    stat = netSend(buf, sz_rfbClientCutTextMsg + len) ;
    free(buf)   ;
    return stat ;
}
