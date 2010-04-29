/* RHSrvAny - Turn any Windows program into a Windows service.
 * Written by Yuval Kashtan.
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <windows.h>
#include <tchar.h>
#include <strsafe.h>

#include "RHSrvAny.h"

#define SVCNAME TEXT("RHEV-apt")

SERVICE_STATUS gSvcStatus; 
HANDLE ghSvcStopEvent = NULL;
SERVICE_STATUS_HANDLE gSvcStatusHandle; 

VOID SvcInstall (void);
VOID WINAPI SvcCtrlHandler (DWORD); 
VOID WINAPI SvcMain (DWORD, LPTSTR *); 

VOID SvcReportEvent (LPTSTR);
VOID SvcInit (DWORD, LPTSTR *); 
VOID ReportSvcStatus (DWORD, DWORD, DWORD);

void	
__cdecl 
_tmain (
	int argc, 
	TCHAR *argv[]
) { 
	SERVICE_TABLE_ENTRY DispatchTable[] = { 
		{ 
			SVCNAME, 
			(LPSERVICE_MAIN_FUNCTION) SvcMain 
		}, 
		{ NULL, NULL } 
	}; 

	if( 
		lstrcmpi( 
			argv[1], 
			TEXT("install")
		) == 0 
	) {
		SvcInstall();
		return;
	}

	if (!StartServiceCtrlDispatcher( DispatchTable )) 
    { 
        SvcReportEvent(TEXT("StartServiceCtrlDispatcher")); 
    } 
} 

VOID 
SvcInstall() {
	SC_HANDLE schSCManager;
	SC_HANDLE schService;
	TCHAR szPath[MAX_PATH];

	if ( 
		!GetModuleFileName( 
			NULL, 
			szPath, 
			MAX_PATH 
		) 
	) {
		printf("Cannot install service (%d)\n", GetLastError());
		return;
	}

    schSCManager = OpenSCManager( 
		NULL,
		NULL,
		SC_MANAGER_ALL_ACCESS
	);
 
    if (NULL == schSCManager) {
		printf("OpenSCManager failed (%d)\n", GetLastError());
		return;
    }

    schService = CreateService ( 
        schSCManager,
        SVCNAME,
        SVCNAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        szPath,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
	);
 
    if (schService == NULL) {
        printf (
			"CreateService failed (%d)\n", 
			GetLastError()
		); 
        CloseServiceHandle (schSCManager);
        return;
    }
	else {
		printf("Service installed successfully\n");
	}

    CloseServiceHandle (schService); 
    CloseServiceHandle (schSCManager);
}

VOID 
WINAPI 
SvcMain (
	DWORD dwArgc, 
	LPTSTR *lpszArgv
) {
	gSvcStatusHandle = RegisterServiceCtrlHandler ( 
		SVCNAME, 
		SvcCtrlHandler
	);

	if (!gSvcStatusHandle) { 
		SvcReportEvent(TEXT("RegisterServiceCtrlHandler")); 
		return; 
	} 

	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
	gSvcStatus.dwServiceSpecificExitCode = 0;    

	ReportSvcStatus (
		SERVICE_START_PENDING, 
		NO_ERROR, 
		3000 
	);

	SvcInit (
		dwArgc,
		lpszArgv 
	);
}

BOOL
RegistryRead (
	HKEY hHive,
	wchar_t *szKeyPath,
	wchar_t *szValue,
	wchar_t *szData,
	DWORD *nSize
) {
	HKEY hKey;
	long lSuccess;

	lSuccess = RegOpenKey (
		hHive,
		szKeyPath,
		&hKey
	);

	if (lSuccess == ERROR_SUCCESS) {
		lSuccess = RegQueryValueEx (
			hKey,
			szValue,
			NULL,
			NULL,
			(LPBYTE) szData,
			nSize
		);

		if (lSuccess == ERROR_SUCCESS) {
			return (TRUE);
		}
	}

	return (FALSE);
}

VOID 
SvcInit (
	DWORD dwArgc, 
	LPTSTR *lpszArgv
) {
	DWORD nSize;
	BOOL fSuccess;
    STARTUPINFO si;
	wchar_t szPWD[1024];
    PROCESS_INFORMATION pi;
	wchar_t szCmdLine[1024];
	wchar_t szRegistryPath[1024];

	// TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with 
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.

    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.

	ghSvcStopEvent = CreateEvent (
		NULL,
		TRUE,
		FALSE,
		NULL
	);

    if (ghSvcStopEvent == NULL) {
        ReportSvcStatus (
			SERVICE_STOPPED, 
			NO_ERROR, 
			0 
		);
        return;
    }

    // Report running status when initialization is complete.
    ReportSvcStatus (
		SERVICE_RUNNING, 
		NO_ERROR, 
		0 
	);

    // TO_DO: Perform work until service stops.
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );
	nSize=1024;

	swprintf_s (
		szRegistryPath,
		nSize,
		L"SYSTEM\\CurrentControlSet\\services\\%s\\Parameters",
		SVCNAME
	);

	fSuccess = RegistryRead (
		HKEY_LOCAL_MACHINE,
		szRegistryPath,
		L"CommandLine",
		szCmdLine,
		&nSize
	);

	if (fSuccess) {
		fSuccess = RegistryRead (
			HKEY_LOCAL_MACHINE,
			szRegistryPath,
			L"PWD",
			szPWD,
			&nSize
		);
	}

	if (fSuccess) {
		fSuccess = CreateProcess (
			NULL,
			szCmdLine,
			NULL,
			NULL,
			FALSE,
			(
				CREATE_NO_WINDOW
			),
			NULL,
			szPWD,
			&si,
			&pi
		);
	}

	// treat errors

    while(1)
    {
        // Check whether to stop the service.
        WaitForSingleObject (
			ghSvcStopEvent, 
			INFINITE
		);

        ReportSvcStatus (
			SERVICE_STOPPED, 
			NO_ERROR, 
			0 
		);

        return;
    }
}

VOID ReportSvcStatus (
	DWORD dwCurrentState,
	DWORD dwWin32ExitCode,
	DWORD dwWaitHint
) {
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.
    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING) {
		gSvcStatus.dwControlsAccepted = 0;
	}
	else {
		gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	}

	if (
		(dwCurrentState == SERVICE_RUNNING) ||
		(dwCurrentState == SERVICE_STOPPED)
	) {
		gSvcStatus.dwCheckPoint = 0;
	} else {
		gSvcStatus.dwCheckPoint = dwCheckPoint++;
	}

	// Report the status of the service to the SCM.
	SetServiceStatus (
		gSvcStatusHandle, 
		&gSvcStatus
	);
}

VOID 
WINAPI 
SvcCtrlHandler (
	DWORD dwCtrl
) {
   switch(dwCtrl) {  
      case SERVICE_CONTROL_STOP: 
         ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

         /* Signal the service to stop. */
         SetEvent(ghSvcStopEvent);
         
         return;
 
      case SERVICE_CONTROL_INTERROGATE: 
         /* Fall through to send current status. */
         break; 
 
      default: 
         break;
   } 

   ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
}

/* Logs messages to the event log */
VOID 
SvcReportEvent (
	LPTSTR szFunction
) { 
    TCHAR Buffer[80];
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];

    hEventSource = RegisterEventSource (
		NULL, 
		SVCNAME
	);

    if (
		NULL != hEventSource
	) {
		StringCchPrintf (
			Buffer, 
			80, 
			TEXT("%s failed with %d"), 
			szFunction, 
			GetLastError()
		);

        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;

		ReportEvent (
			hEventSource,
			EVENTLOG_ERROR_TYPE,
			0,
			SVC_ERROR,
			NULL,
			2,
			0,
			lpszStrings,
			NULL
		);

		DeregisterEventSource (hEventSource);
    }
}