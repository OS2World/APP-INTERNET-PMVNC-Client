/*
 * network.c - PM VNC Viewer, Networking Supports
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef __WATCOMC__
#include <process.h>
#endif

#define INCL_DOS
#define INCL_PM
#include <os2.h>

#include "pmvncdef.h"

/* MKG: keep track of Network thread ID */
int     NetworkTID;

extern int deadthread;

HAB     habNetwork = NULLHANDLE ;
HMQ     hmqNetwork = NULLHANDLE ;

// MKG 1.01 - SEM to signal thread start/stop - Mike likes SEMs :)
extern HEV    hevNETSTARTSEM;
extern ULONG  NETSTARTSEMCt;

extern HEV    hevNETSTOPSEM;
extern ULONG  NETSTOPSEMCt;

/*
 * VNC Server and Session Options
 */

#define VNCPORTBASE     (5900)

static  int     ServerSock ;

/*
 * Notifications to Window Thread
 */

void    netFail(PUCHAR msg)
{
    WinPostMsg(hwndClient, WM_VNC_FAIL, NULL, MPFROMP(msg)) ;
}

void    netNotify(int ev, MPARAM mp1, MPARAM mp2)
{
    WinPostMsg(hwndClient, ev, mp1, mp2) ;
}

/*
 * thread control variables
 */

// static  BOOL    stopThread = FALSE ;
//static  BOOL    doneThread = FALSE ;

/*
 * netSend/Recv - network I/O with exact length
 */

BOOL    netSend(PUCHAR buf, int len)
{
    int     cnt, num ;

    for (cnt = 0 ; cnt < len ;  ) {
        if(NETSTOPSEMCt) {
#ifdef DEBUG
    TRACE("NetThread: Network thread stop sig3\n") ;
#endif
            return FALSE ;
        }
        if ((num = send(ServerSock, (buf + cnt), (len - cnt), 0)) <= 0) {
            return FALSE ;
        }
        cnt += num ;
    }
    return TRUE ;
}

BOOL    netRecv(PUCHAR buf, int len)
{
    int     cnt, num ;

    for (cnt = 0 ; cnt < len ;  ) {
        if(NETSTOPSEMCt) {
#ifdef DEBUG
    TRACE("NetThread: Network thread stop sig2\n") ;
#endif
            return FALSE ;
        }
        if ((num = recv(ServerSock, (buf + cnt), (len - cnt), 0)) <= 0) {
            return FALSE ;
        }
        cnt += num ;
    }
    return TRUE ;
}

void    netDump(PUCHAR buf, int len)
{
    int     cnt ;

    for (cnt = 0 ; cnt < len ; cnt++) {
        printf("%02x ", (buf[cnt] & 0xff)) ;
        if ((cnt % 16) == 15) {
            printf("\n") ;
        }
    }
    printf("\n") ;
}

/*
 * netThread - networking thread
 */

static  void    netThread(void *arg)
{
    UCHAR   host[256] ;
    UCHAR   mesg[256] ;
    ULONG   port      ;
    ULONG   ipaddr    ;
    struct  sockaddr_in server ;
    struct  hostent     *hp    ;

    if (sscanf(SessServerName, "%[^:]:%d", host, &port) != 2) {
        netFail("bad server spec.") ;
        ++deadthread;
        _endthread();
    }
    if (port < 100) {
        port += VNCPORTBASE ;
    }

    // tell main I am running
    DosPostEventSem(hevNETSTARTSEM);

#ifdef DEBUG
    TRACE("NetThread: Connect to Server %s:%d\n", host, port) ;
#endif

    if (isdigit(host[0])) {
        ipaddr = inet_addr(host) ;
    } else if ((hp = gethostbyname(host)) != NULL) {
        ipaddr = *((ULONG *) hp->h_addr) ;
    } else {
        netFail("no such host") ;
        ++deadthread;
        _endthread();
    }

    if ((ServerSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        netFail("failed to create socket") ;
        ++deadthread;
        _endthread();
    }

    memset(&server, 0, sizeof(server)) ;
    server.sin_family      = AF_INET ;
    server.sin_addr.s_addr = ipaddr  ;
    server.sin_port        = htons((USHORT) port) ;

    if (connect(ServerSock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        sprintf(mesg, "failed to connect (%d)", errno) ;
        netFail(mesg) ;
        ++deadthread;
        _endthread();
    }

#ifdef DEBUG
    TRACE("NetThread: Connected\n") ;
#endif

    habNetwork = WinInitialize(0) ;
    hmqNetwork = WinCreateMsgQueue(habNetwork, 0) ;

#ifdef DEBUG
    TRACE("NetThread: protoConnInit()\n") ;
#endif

    if (protoConnInit() != TRUE) {
        WinDestroyMsgQueue(hmqNetwork) ;
        WinTerminate(habNetwork) ;
        ++deadthread;
        _endthread();
    }

#ifdef DEBUG
    TRACE("NetThread: protoSendFmtEnc()\n") ;
#endif

    if (protoSendFmtEnc() != TRUE) {
        WinDestroyMsgQueue(hmqNetwork) ;
        WinTerminate(habNetwork) ;
        ++deadthread;
        _endthread();
    }

#ifdef DEBUG
    TRACE("NetThread: protoSendRequest()\n") ;
#endif

    if (protoSendRequest(FALSE, NULL) != TRUE) {
        WinDestroyMsgQueue(hmqNetwork) ;
        WinTerminate(habNetwork) ;
        ++deadthread;
        _endthread();
    }

    /* here is the real network action */
    while (TRUE) {
        DosQueryEventSem(hevNETSTOPSEM, &NETSTOPSEMCt);
        if(NETSTOPSEMCt) {
#ifdef DEBUG
            TRACE("NetThread: Network thread stop sig1\n") ;
#endif
            break;
        }
        if (protoDispatch( ) != TRUE) break;
    }

#ifdef DEBUG
    TRACE("NetThread: Network thread ending... ") ;
#endif

    (*VncCtx->rectDone) () ;

    WinDestroyMsgQueue(hmqNetwork) ;
    WinTerminate(habNetwork) ;

#ifdef DEBUG
    TRACE("done\n") ;
#endif

    // reuse the start SEM to signal shutdown
    DosPostEventSem(hevNETSTARTSEM);

    _endthread();
}

/*
 * netStartup - startup networking thread
 *
 * MKG: 1.01 changed to keep the TID
 */

#define STKSIZ  (1024 * 64)     /* 32KB caused stack overflow */

BOOL    netStartup(HAB hab)
{
    NetworkTID = _beginthread(netThread, NULL, STKSIZ, NULL);

    if(NetworkTID == -1) {
        netFail("failed to start network thread") ;
        return FALSE ;
    }

#ifdef DEBUG
    TRACE("Network thread started - TID: %d\n", NetworkTID) ;
#endif

    return TRUE;
}

/*
 * netFinish - terminate networking thread
 */

void    netFinish(HAB hab)
{
#ifdef DEBUG
    TRACE("Network thread TID %d stopping\n", NetworkTID) ;
#endif

    DosPostEventSem(hevNETSTOPSEM);

    // MKG: original emx version had - close(ServerSock) ;
    // seems it was suppose to set a variable and kill the
    // socket which is blocking to end the thread
    if(shutdown(ServerSock, 2) != 0) netFail("failed to shutdown socket.") ;

    soclose(ServerSock) ;

    // reuse start SEM - not really needed but I want to make sure
    // the network thread ends nice
    if(DosWaitEventSem(hevNETSTARTSEM, 5000))
    {
        netFail("No network thread shutdown SEM signal.") ;
    }

    return ;
}
