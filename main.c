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
 * Description:  Main PM VNC Viewer, program entry. Original PM Viewer
 *               compiled with emx/gcc, but now it is OW only. Main changes
 *               were to add SEMs for shutdown and program name/path find.
 *
 ***************************************************************************/


#ifdef DEBUG
#define INCL_DOSPROCESS
#endif
#define INCL_DOS
#define INCL_PM

#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#include "pmvncdef.h"
#include "pmvncres.h"


/*
 * Name and Version of this Program
 */
char   ProgramVers[] = "PM VNC Viewer, Version 1.03 OW" ;

/*
 * old myname - trim and save Program Names
 */
char   ProgramPath[_MAX_PATH] ;
char   ProgramName[_MAX_FNAME] ;
char   ProfilePath[_MAX_PATH] ;

int deadthread = 0;

// MKG 1.01 - SEM to signal thread start/stop - Mike likes SEMs :)
char   SEM_1[_MAX_PATH];
PSZ    NETSTARTSEM = SEM_1;
HEV    hevNETSTARTSEM = 0;
ULONG  NETSTARTSEMCt  = 0;

char   SEM_2[_MAX_PATH];
PSZ    NETSTOPSEM = SEM_2;
HEV    hevNETSTOPSEM = 0;
ULONG  NETSTOPSEMCt  = 0;

#ifdef DEBUG
void MorphToPM( void );
#endif


/*
 * Program Start here
 */
int main(int argc, char *argv[])
{
    HAB   hab  ;
    HMQ   hmq  ;
    QMSG  qmsg ;

    // MKG 1.03 use for path name
    char drive[ _MAX_DRIVE ];
    char dir[ _MAX_DIR ];
    char ext[ _MAX_EXT ];

    // MKG: 1.02 Randomize SEM names so more than one
    // client can be started
    char *SEMNAME = "\\SEM32\\PMVNC\\NETTHREAD";
    sprintf(SEM_1, "%s_1_%d", SEMNAME, getpid( ) );
    sprintf(SEM_2, "%s_2_%d", SEMNAME, getpid( ) );

    /*
     * Initializing
     */
#ifdef DEBUG
    // morph to PM if DEBUG
    MorphToPM( );
    setbuf( stdout, NULL);
    setbuf( stdin, NULL);
#endif

    // MKG 1.03: replaced myname function with splitpath
    strcpy(ProgramPath, argv[0]);

    _splitpath(argv[0], drive, dir, ProgramName, ext);

    // profile path and name
    _makepath(ProfilePath, drive, dir, ProgramName, ".INI");

    sessParse(argc, argv) ;

    hab = WinInitialize(0) ;
    hmq = WinCreateMsgQueue(hab, 0) ;

    if (winCreate(hab) != TRUE) {
        errMessage("failed to create window") ;
        WinDestroyMsgQueue(hmq) ;
        WinTerminate(hab) ;
        return 1 ;
    }
    if (sessSetup(hab) != TRUE) {
        winDispose(hab) ;
        WinDestroyMsgQueue(hmq) ;
        WinTerminate(hab) ;
        return 1 ;
    }

    // MKG 1.01: Added SEMs form NET thread shutdown signal
    if(DosCreateEventSem(NETSTARTSEM, &hevNETSTARTSEM, DC_SEM_SHARED, FALSE))
    {
#ifdef DEBUG
    TRACE("Network start SEM not created\n") ;
#endif
        winDispose(hab) ;
        WinDestroyMsgQueue(hmq) ;
        WinTerminate(hab) ;
        return 1 ;
    }

    if(DosCreateEventSem(NETSTOPSEM, &hevNETSTOPSEM, DC_SEM_SHARED, FALSE))
    {
#ifdef DEBUG
        TRACE("Network stop SEM not created\n") ;
#endif
        winDispose(hab) ;
        WinDestroyMsgQueue(hmq) ;
        WinTerminate(hab) ;
        return 1 ;
    }
#ifdef DEBUG
    else TRACE("Network SEMs created\n") ;
#endif

    if(!netStartup(hab))
    {
        winDispose(hab) ;
        WinDestroyMsgQueue(hmq) ;
        WinTerminate(hab) ;
        return 1 ;
    }

    if(DosWaitEventSem(hevNETSTARTSEM, 5000))
    {
#ifdef DEBUG
        TRACE("Network start SEM signal thread timeout.\n") ;
#endif
        winDispose(hab) ;
        WinDestroyMsgQueue(hmq) ;
        WinTerminate(hab) ;
        return 1 ;
    } else {
#ifdef DEBUG
        TRACE("Network start SEM signal thread start.\n") ;
#endif
        // could destroy SEM now but keep around in case needed later
        DosResetEventSem(hevNETSTARTSEM, &NETSTARTSEMCt);
    }

    /*
     * Window Processing
     */
    while (WinGetMsg(hab, &qmsg, 0, 0, 0)) {
        WinDispatchMsg(hab, &qmsg) ;
    }

#ifdef DEBUG
        TRACE("Shutdown started....\n") ;
#endif

    sessSaveProfile(hab) ;
    kmapFree( ) ;

    /*
     * Dispose Resources
     */

    // MKG 1.01: if the network thread is running
    // shut it down
    if(!deadthread) {
        netFinish(hab);
#ifdef DEBUG
        TRACE("Thread still kicking...\n") ;
    } else TRACE("Thread already dead...\n") ;
#else
    }
#endif

    winDispose(hab) ;

    WinDestroyMsgQueue(hmq) ;
    WinTerminate(hab) ;

    return 0 ;
}

/*
 * Place Dialog at Center of Screen
 */
void dialogAtCenter(HWND hwndDialog)
{
    SWP posDlg ;
    SWP posScr ;

    WinQueryWindowPos(HWND_DESKTOP, &posScr) ;
    WinQueryWindowPos(hwndDialog,   &posDlg) ;

    posDlg.x = (posScr.cx - posDlg.cx) / 2 ;
    posDlg.y = (posScr.cy - posDlg.cy) / 2 ;

    WinSetWindowPos(hwndDialog, NULLHANDLE,
                    posDlg.x, posDlg.y, 0, 0, SWP_MOVE) ;
}

/*
 * Place Dialog near the mouse pointer
 */
void dialogAtMouse(HWND hwndDialog, int target)
{
    HWND    hwndTarget = WinWindowFromID(hwndDialog, target) ;
    POINTL  pt ;
    SWP     posDlg ;
    SWP     posScr ;
    SWP     posCtl ;

    WinQueryPointerPos(HWND_DESKTOP, &pt)    ;
    WinQueryWindowPos(HWND_DESKTOP, &posScr) ;
    WinQueryWindowPos(hwndDialog,   &posDlg) ;

    if (hwndTarget != NULLHANDLE) {
        WinQueryWindowPos(hwndTarget, &posCtl) ;
    } else {
        posCtl.x  = posCtl.y  = 0 ;
        posCtl.cx = posCtl.cy = 0 ;
    }

    posDlg.x = pt.x - posCtl.x ;
    posDlg.y = pt.y - posCtl.y - (posCtl.cy / 2) ;

    if (posDlg.x < 0) {
        posDlg.x = 0 ;
    }
    if (posDlg.y < 0) {
        posDlg.y = 0 ;
    }
    if ((posDlg.x + posDlg.cx) > posScr.cx) {
        posDlg.x = posScr.cx - posDlg.cx ;
    }
    if ((posDlg.y + posDlg.cy) > posScr.cy) {
        posDlg.y = posScr.cy - posDlg.cy ;
    }
    WinSetWindowPos(hwndDialog, NULLHANDLE,
                    posDlg.x, posDlg.y, 0, 0, SWP_MOVE) ;
}
/*
 * Error Notify
 */
void errMessage(PSZ msg)
{
    WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, msg, ProgramVers, 0, MB_OK);
}

#ifdef DEBUG
/*
 * Doodle's Morph to PM to get printf to the console
 * MKG 1.01: Added to get debug info on console from TRACE
 */
void MorphToPM( void )
{
    PPIB pib;
    PTIB tib;

    DosGetInfoBlocks(&tib, &pib);

    // Change flag from VIO to PM:
    if (pib->pib_ultype==2) pib->pib_ultype = 3;
}
#endif

