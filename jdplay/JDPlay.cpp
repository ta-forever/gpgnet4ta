/*	Copyright 2007 	Edwin Stang (edwinstang@gmail.com)

    This file is part of JDPlay.

    JDPlay is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    JDPlay is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with JDPlay.  If not, see <http://www.gnu.org/licenses/>.
*/

/****************************************************************************************************************/

/*	This is a class to the DirectPlay RippleLaunch technology for easy use.
 *
 *	Needs to be linked to: "dplayx.lib dxguid.lib"
 *
 *  You will need the DirectX SDK April 2007 (latest version with all DirectPlay headers).
 *  Also you will need the Windows Platform SDK for ATL.
 */

/****************************************************************************************************************/

#include "JDPlay.h"
#include <iostream>
#include <windowsx.h>	//GlobalAllocPtr
#include <iomanip>
#include <sstream>
#include "DPlayWrapper.h"

inline BSTR _ConvertStringToBSTR(const char* pSrc)
{
    if(!pSrc) return NULL;

    DWORD cwch;

    BSTR wsOut(NULL);

    if(cwch = ::MultiByteToWideChar(CP_ACP, 0, pSrc,
         -1, NULL, 0))//get size minus NULL terminator
    {
                cwch--;
            wsOut = ::SysAllocStringLen(NULL, cwch);

        if(wsOut)
        {
            if(!::MultiByteToWideChar(CP_ACP,
                     0, pSrc, -1, wsOut, cwch))
            {
                if(ERROR_INSUFFICIENT_BUFFER == ::GetLastError())
                    return wsOut;
                ::SysFreeString(wsOut);//must clean up
                wsOut = NULL;
            }
        }

    };

    return wsOut;
};

std::string ToAnsi(LPWSTR s)
{
    std::string result;
    WCHAR* ptr = s;
    for (; *ptr != 0u; ++ptr)
    {
        result.push_back(char(*ptr));
    }
    result.push_back(0);
    return result;
}

std::string FromAnsi(const char* s)
{
    std::string result;
    for (; *s != 0u; ++s)
    {
        result.push_back(*s);
        result.push_back(0);
    }
    result.push_back(0);
    return result;
}

void FromAnsi(const char* ansi, LPWSTR dest, std::size_t maxBytes)
{
    for (; *ansi != 0u && maxBytes>3u; ++ansi, ++dest, maxBytes -= 2)
    {
        *dest = *ansi;
    }
    *dest = 0;
}

using namespace std;

JDPlay* JDPlay::instance;

BOOL FAR PASCAL EnumSessionsCallback(LPCDPSESSIONDESC2 lpThisSD, LPDWORD lpdwTimeOut, DWORD dwFlags, LPVOID lpContext){
	if(lpThisSD){
		JDPlay::getInstance()->updateFoundSessionDescription(lpThisSD);
	}
	return 0;
}

BOOL FAR PASCAL enumPlayersCallback(
    DPID            dpId,
    DWORD           dwPlayerType,
    LPCDPNAME       lpName,
    DWORD           dwFlags,
    LPVOID          lpContext)
{
    cout << "\rdpID:" << dpId
        << ", dwPlayerType:" << (dwPlayerType == DPPLAYERTYPE_PLAYER ? "Player" : "Group")
        << ", lpName:" << ToAnsi(lpName->lpszShortName);
    if (dwFlags & DPENUMGROUPS_SHORTCUT) cout << ", SHORTCUT";
    if (dwFlags & DPENUMGROUPS_STAGINGAREA) cout << ", STAGING";
    if (dwFlags & DPENUMPLAYERS_GROUP) cout << ", PLYRS&GRPS";
    if (dwFlags & DPENUMPLAYERS_LOCAL) cout << ", LOCAL";
    if (dwFlags & DPENUMPLAYERS_REMOTE) cout << ", REMOTE";
    if (dwFlags & DPENUMPLAYERS_SESSION) cout << ", SESSION";
    if (dwFlags & DPENUMPLAYERS_SERVERPLAYER) cout << ", SERVER";
    if (dwFlags & DPENUMPLAYERS_SPECTATOR) cout << ", SPECTATOR";
    cout << std::endl;
    return true;
}

JDPlay::JDPlay(const char* playerName, int searchValidationCount, bool debug){

	if(debug){
		cout << "++ JDPlay(" << playerName << "," << searchValidationCount << "," << debug << ")" << endl;
		fflush(stdout);
	}

	this->instance = this;
	this->searchValidationCount = searchValidationCount;
	this->lpDPIsOpen = false;
	this->isInitialized = false;
	this->debug = debug;

	// clear out memory for info objects
	ZeroMemory(&dpName,sizeof(DPNAME));
	ZeroMemory(&dpSessionDesc, sizeof(DPSESSIONDESC2));
	ZeroMemory(&dpConn, sizeof(DPLCONNECTION));

	// populate player info
	dpName.dwSize = sizeof(DPNAME);
	dpName.dwFlags = 0; // not used, must be zero
	dpName.lpszLongName = (LPWSTR)malloc(256);
	dpName.lpszShortName = (LPWSTR)malloc(256);
	updatePlayerName(playerName);

	if(debug){
		cout << "-- JDPlay()" << endl;
		fflush(stdout);
	}
}

JDPlay::~JDPlay(){

	if(debug){
		cout << "++ ~JDPlay()" << endl;
		fflush(stdout);
	}

        if (isInitialized) {
            deInitialize();
        }

	if(debug){
		cout << "-- ~JDPlay()" << endl;
		fflush(stdout);
	}
}


bool JDPlay::initialize(const char* gameGUID, const char* hostIP, bool isHost, int maxPlayers){

	if(debug){
		cout << "++ initialize(" << gameGUID << ", " << hostIP << ", " << isHost << ")" << endl;
		fflush(stdout);
	}

	if(isInitialized){
		deInitialize();
	}

	HRESULT hr;
	
	// create GUID Object **************************************************************************
	GUID gameID;
	
	LPOLESTR lpoleguid = _ConvertStringToBSTR(gameGUID);
	hr = CLSIDFromString(lpoleguid, &gameID);
	::SysFreeString(lpoleguid);

	if(hr != S_OK){
		if(debug){
			cout << "initialize() - ERROR: invalid GUID" << endl;
			fflush(stdout);
		}
		return false;
	}

	if(debug){
		cout << "initialize() - GUID initialized" << endl;
		fflush(stdout);
	}
	
	// create TCP connection ***********************************************************************
	LPDIRECTPLAYLOBBY old_lpDPLobby = NULL;      // old lobby pointer
	DPCOMPOUNDADDRESSELEMENT  address[2];        // to create compound addr
	DWORD     addressSize = 0;					// size of compound address
	LPVOID    lpConnection = NULL;				// pointer to make connection

    if (true)
    {
        // registering COM
        hr = CoInitialize(NULL);

        if (hr != S_OK) {
            if (debug) {
                cout << "initialize() - ERROR: failed to initialize COM" << endl;
                fflush(stdout);
            }
            return false;
        }

        if (debug) {
            cout << "initialize() - COM initialized" << endl;
            fflush(stdout);
        }

        // creating directplay object
        hr = CoCreateInstance(CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay3, (LPVOID*)&lpDP);

        if (hr != S_OK) {
            if (debug) {
                cout << "initialize() - ERROR: failed to initialize DirectPlay" << endl;
                fflush(stdout);
            }
            return false;
        }

        CoUninitialize();  // unregister the COM

        if (debug) {
            cout << "initialize() - initialized DirectPlay and deinitialized COM" << endl;
            fflush(stdout);
        }

        // creating lobby object
        hr = DirectPlayLobbyCreate(NULL, &old_lpDPLobby, NULL, NULL, 0);

        if (hr != S_OK) {
            if (debug) {
                cout << "initialize() - ERROR[" << getDPERR(hr) << "]: failed to create lobby object" << endl;
                fflush(stdout);
            }
            return false;
        }

        // get new interface of lobby
        hr = old_lpDPLobby->QueryInterface(IID_IDirectPlayLobby3, (LPVOID*)&lpDPLobby);

        if (hr != S_OK) {
            if (debug) {
                cout << "initialize() - ERROR[" << getDPERR(hr) << "]: failed to get new lobby interface" << endl;
                fflush(stdout);
            }
            return false;
        }
    }
    else
    {
        GUID guid;
        std::memset(&guid, 0, sizeof(guid));
        LPDIRECTPLAY dp1;
        hr = DPlayWrapper().directPlayCreate(&guid, &dp1, NULL);
        if (hr != S_OK) {
            std::cout << std::hex << hr << " unable to create DirectPlay" << std::endl;
            return false;
        }
        hr = dp1->QueryInterface(IID_IDirectPlay3, (LPVOID*)&lpDP);
        if (hr != S_OK) {
            std::cout << std::hex << hr << " unable to get DirectPlay3" << std::endl;
            return false;
        }
        hr = DPlayWrapper().directPlayLobbyCreate(NULL, &old_lpDPLobby, NULL, NULL, 0);
        if (hr != S_OK) {
            std::cout << std::hex << hr << " unable to create DirectPlayLobby" << std::endl;
            return false;
        }
        hr = old_lpDPLobby->QueryInterface(IID_IDirectPlayLobby3, (LPVOID*)&lpDPLobby);
        if (hr != S_OK) {
            std::cout << std::hex << hr << " unable to get DirectPlayLobby3" << std::endl;
            return false;
        }
    }

	// release old interface since we have new one
	hr = old_lpDPLobby->Release();

	if(hr != S_OK){
		if(debug){
			cout << "initialize() - ERROR[" << getDPERR(hr) << "]: failed to release old lobby interface" << endl;
			fflush(stdout);
		}
		return false;
	}

	if(debug){
		cout << "initialize() - lobby initialized" << endl;
		fflush(stdout);
	}

	// fill in data for address
	address[0].guidDataType = DPAID_ServiceProvider;
	address[0].dwDataSize   = sizeof(GUID);
	address[0].lpData       = (LPVOID)&DPSPGUID_TCPIP;  // TCP ID

	if(isHost){ //Bind to any ip?
		hostIP = "";
	}

	address[1].guidDataType = DPAID_INet;
	address[1].dwDataSize   = static_cast<DWORD>(strlen(hostIP)+1);
	address[1].lpData       = const_cast<char*>(hostIP);

	// get size to create address
	// this method will return DPERR_BUFFERTOOSMALL, that is not an error
	hr = lpDPLobby->CreateCompoundAddress(address, 2, NULL, &addressSize);

	if(hr != S_OK && hr != DPERR_BUFFERTOOSMALL){
		if(debug){
			cout << "initialize() - ERROR[" << getDPERR(hr) << "]: failed to get size for CompoundAddress" << endl;
			fflush(stdout);
		}
		return false;
	}

	lpConnection = GlobalAllocPtr(GHND, addressSize);  // allocating mem

	// now creating the address
	hr = lpDPLobby->CreateCompoundAddress(address, 2, lpConnection, &addressSize);

	if(hr != S_OK){
		if(debug){
			cout << "initialize() - ERROR[" << getDPERR(hr) << "]: failed to create CompoundAddress" << endl;
			fflush(stdout);
		}
		return false;
	}

	// initialize the tcp connection
	hr = lpDP->InitializeConnection(lpConnection, 0);

	if(hr != S_OK){
		if(debug){
			cout << "initialize() - ERROR[" << getDPERR(hr) << "]: failed to initialize TCP connection" << endl;
			fflush(stdout);
		}
		return false;
	}

	if(debug){
		cout << "initialize() - TCP connection initialized" << endl;
		fflush(stdout);
	}

	// populate session description ******************************************************************
	dpSessionDesc.dwSize = sizeof(DPSESSIONDESC2);
	dpSessionDesc.dwFlags = 0;									// optional: DPSESSION_MIGRATEHOST
	dpSessionDesc.guidApplication = gameID;						// Game GUID
	dpSessionDesc.guidInstance = gameID;						// ID for the session instance
	dpSessionDesc.lpszSessionName = L"Coopnet Session";			// name of the session
	dpSessionDesc.dwMaxPlayers = maxPlayers;					// Maximum # players allowed in session
	dpSessionDesc.dwCurrentPlayers = 0;							// Current # players in session (read only)
	dpSessionDesc.lpszPassword = L"\0";							// password of the session (optional)
	dpSessionDesc.dwReserved1 = 0;								// Reserved for future M$ use.
	dpSessionDesc.dwReserved2 = 0;
	dpSessionDesc.dwUser1 = 0;									// For use by the application
	dpSessionDesc.dwUser2 = 0;
	dpSessionDesc.dwUser3 = 0;
	dpSessionDesc.dwUser4 = 0;

	// populate connection info **********************************************************************
	dpConn.dwSize = sizeof(DPLCONNECTION);
	dpConn.lpSessionDesc = &dpSessionDesc;		// Pointer to session desc to use on connect
	dpConn.lpPlayerName = &dpName;			// Pointer to Player name structure
	dpConn.guidSP = DPSPGUID_TCPIP;			// GUID of the DPlay SP to use
	dpConn.lpAddress = lpConnection;		// Address for service provider
	dpConn.dwAddressSize = addressSize;		// Size of address data
	if(isHost){
		dpConn.dwFlags = DPLCONNECTION_CREATESESSION;
	}else{
		dpConn.dwFlags = DPLCONNECTION_JOINSESSION;
	}

	if(debug){
		cout << "initialize() - session info configured" << endl;
		fflush(stdout);
	}

	// set other vars
	if(isHost){
		sessionFlags = DPOPEN_CREATE;
		playerFlags = DPPLAYER_SERVERPLAYER;
	}else{
		sessionFlags = DPOPEN_JOIN;
		playerFlags = 0;
	}

	isInitialized = true;

	if(debug){
		cout << "-- initialize()" << endl;
		fflush(stdout);
	}

	return true;
}

bool JDPlay::isHost(){
	return sessionFlags != DPOPEN_JOIN;
}

void JDPlay::updatePlayerName(const char* playerNameA){
    FromAnsi(playerNameA, dpName.lpszShortName, 256);
    FromAnsi(playerNameA, dpName.lpszLongName, 256);
	
	if(debug){
		cout << "updatePlayerName() - playername set to \"" << playerNameA << "\"" << endl;
		cout << "-- updatePlayerName()" << endl;
		fflush(stdout);
	}
}
bool JDPlay::searchOnce(){
	if(debug){
		cout << "++ searchOnce()" << endl;
		fflush(stdout);
	}
	
	HRESULT hr;

	if(!isHost()){

		foundLobby = false;

		if(lpDP){

			hr = lpDP->EnumSessions(&dpSessionDesc, 0, EnumSessionsCallback, NULL, 0);
			if(hr != S_OK){
				if(debug){
					cout << endl << "searchOnce() - ERROR[" << getDPERR(hr) << "]: failed to enumerate sessions" << endl;
					fflush(stdout);
				}
				return false;
			}

			if(!foundLobby){
				if(debug){
					cout << "searchOnce() - no session found" << endl;
					cout << "-- searchOnce()" << endl;
					fflush(stdout);
				}

				return false;
			}
		}

		if(lpDP){

			hr = lpDP->Open(&dpSessionDesc, sessionFlags | DPOPEN_RETURNSTATUS);
			if(hr != S_OK){
				if(debug){
					cout << "searchOnce() - ERROR[" << getDPERR(hr) << "]: failed to open DirectPlay session" << endl;
					fflush(stdout);
				}
				return false;
			}

			lpDPIsOpen = true;

			// create player *******************************************************************************
			//hr = lpDP->CreatePlayer(&dPid, &dpName, NULL, NULL, 0, playerFlags);

			//if(hr != S_OK){
			//	if(debug){
			//		cout << "searchOnce() - ERROR[" << getDPERR(hr) << "]: failed to create local player" << endl;
			//		fflush(stdout);
			//	}
			//	return false;
			//}

			//if(debug){
			//	cout << "searchOnce() - session opened and player initialized" << endl;
			//	fflush(stdout);
			//}
		}
	}else{
		if(debug){
			cout << "searchOnce() - skipping session search, not needed because hosting" << endl;
			fflush(stdout);
		}
	}

	if(debug){
		cout << "-- searchOnce()" << endl;
		fflush(stdout);
	}

	return true;
}

bool JDPlay::launch(bool startGame){

    if (debug){
        cout << "++ launch()" << endl;
        fflush(stdout);
    }

    if (!isInitialized){
        if (debug){
            cout << "launch() - WARNING: JDPlay has to be initialized before launching!" << endl;
            fflush(stdout);
        }
        return false;
    }

    //{
    //    struct ReleaseDPOnScopeExit
    //    {
    //        JDPlay *jdplay;
    //        ReleaseDPOnScopeExit(JDPlay *jdplay) : jdplay(jdplay) { }
    //        ~ReleaseDPOnScopeExit() { jdplay->releaseDirectPlay(); }
    //    } releaseDP(this);
    //}
    //lpDP->EnumSessions(&dpSessionDesc, 0, EnumSessionsCallback, NULL, 0);
    //releaseDirectPlay();

    if (!startGame)
    {
        return false;
    }

    HRESULT hr;
    // launch game *********************************************************************************
    hr = lpDPLobby->RunApplication(0, &appID, &dpConn, 0);

    if (hr != S_OK){
        if (debug){
            cout << "launch() - ERROR[" << getDPERR(hr) << "]: failed to launch the game, maybe it's not installed properly" << endl;
            fflush(stdout);
        }
        return false;
    }

    if (debug){
        cout << "launch() - game started, ProcessID = " << appID << endl;
        fflush(stdout);
    }

    // wait until game exits ***********************************************************************
    HANDLE appHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, appID);
    if (appHandle == NULL){
        if (debug){
            cout << "launch() - ERROR: failed to open game process" << endl;
            fflush(stdout);
        }
        return false;
    }
    processHandle = appHandle;
    return true;
}

bool JDPlay::pollStillActive(DWORD& exitCode)
{
    GetExitCodeProcess(processHandle, &exitCode);
    return exitCode == STILL_ACTIVE;
}

void JDPlay::pollSessionStatus(LPDIRECTPLAY3 dplay)
{
    HRESULT hr;

    if (dplay)
    {
        hr = dplay->EnumSessions(&dpSessionDesc, 0, &EnumSessionsCallback, NULL, 0);
        if (S_OK != hr)
        {
            cout << "EnumSessions:" << getDPERR(hr) << endl;
        }
        //hr = lpDP->EnumGroups(&dpSessionDesc.guidInstance, enumPlayersCallback, NULL, 0);
        //if (S_OK != hr)
        //{
        //    cout << "EnumGroups:" << getDPERR(hr) << endl;
        //}
    }
    else if (lpDP)
    {
        pollSessionStatus(lpDP);
    }
    else
    {
        hr = CoInitialize(NULL);
        if (S_OK != hr)
        {
            cout << "CoInitialize:" << getDPERR(hr) << endl;
        }
        hr = CoCreateInstance(CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay3, (LPVOID*)&dplay);
        CoUninitialize();  // unregister the COM
        if (S_OK != hr)
        {
            cout << "CoCreateInstance:" << getDPERR(hr) << endl;
        }
        pollSessionStatus(dplay);
        dplay->Close();
        dplay->Release();
    }
}

void JDPlay::printSessionDesc()
{
    cout << "  session name:" << enumCallbackSessionName << endl;
    cout << "      password:" << enumCallbackSessionPassword << endl;
    cout << "currentPlayers:" << dpSessionDesc.dwCurrentPlayers << endl;
    cout << "        dwUser:" << hex 
        << dpSessionDesc.dwUser1 << ' ' 
        << dpSessionDesc.dwUser2 << ' ' 
        << dpSessionDesc.dwUser3 << ' ' 
        << dpSessionDesc.dwUser4 << endl;
}


void JDPlay::updateFoundSessionDescription(LPCDPSESSIONDESC2 lpFoundSD){	
    if (debug){
		cout << "++ updateFoundSessionDescription(" << lpFoundSD << ")" << endl;
		fflush(stdout);
	}
	
	static int validateCount = -1;
		
	if(validateCount > -1) {
		bool areEqual = dpSessionDesc.dwSize == lpFoundSD->dwSize
			&&	dpSessionDesc.dwFlags == lpFoundSD->dwFlags
			&&	dpSessionDesc.guidInstance == lpFoundSD->guidInstance
			&&	dpSessionDesc.guidApplication == lpFoundSD->guidApplication
			&&	dpSessionDesc.dwMaxPlayers == lpFoundSD->dwMaxPlayers
			&&	dpSessionDesc.dwReserved1 == lpFoundSD->dwReserved1
			&&	dpSessionDesc.dwReserved2 == lpFoundSD->dwReserved2
			&&	dpSessionDesc.dwUser1 == lpFoundSD->dwUser1
			&&	dpSessionDesc.dwUser2 == lpFoundSD->dwUser2
			&&	dpSessionDesc.dwUser3 == lpFoundSD->dwUser3
			&&	dpSessionDesc.dwUser4 == lpFoundSD->dwUser4;

		if(areEqual){
			validateCount++;
		}else{
			validateCount = -1;
		}
	}else{
		//so that dplay also joins sessions created ingame
		dpSessionDesc.dwSize = lpFoundSD->dwSize;
		dpSessionDesc.dwFlags = lpFoundSD->dwFlags;
		dpSessionDesc.guidInstance = lpFoundSD->guidInstance;
		dpSessionDesc.guidApplication = lpFoundSD->guidApplication;
		dpSessionDesc.dwMaxPlayers = lpFoundSD->dwMaxPlayers;
		dpSessionDesc.dwCurrentPlayers = lpFoundSD->dwCurrentPlayers;
		dpSessionDesc.dwReserved1 = lpFoundSD->dwReserved1;
		dpSessionDesc.dwReserved2 = lpFoundSD->dwReserved2;
		dpSessionDesc.dwUser1 = lpFoundSD->dwUser1;
		dpSessionDesc.dwUser2 = lpFoundSD->dwUser2;
		dpSessionDesc.dwUser3 = lpFoundSD->dwUser3;
		dpSessionDesc.dwUser4 = lpFoundSD->dwUser4;
		dpSessionDesc.lpszSessionName = lpFoundSD->lpszSessionName;
		dpSessionDesc.lpszPassword = lpFoundSD->lpszPassword;

        validateCount = 0;
	}	

    if (lpFoundSD->lpszSessionName)
    {
        enumCallbackSessionName = ToAnsi(lpFoundSD->lpszSessionName);
    }
    if (lpFoundSD->lpszPassword)
    {
        enumCallbackSessionPassword = ToAnsi(lpFoundSD->lpszPassword);
    }

	if(validateCount >= searchValidationCount){
		foundLobby = true;
	}else{
		foundLobby = false;
	}
}


std::string JDPlay::getAdvertisedPlayerName()
{
    return ToAnsi(dpName.lpszShortName);
}

std::string JDPlay::getAdvertisedSessionName()
{
    return enumCallbackSessionName;
}

void JDPlay::dpClose()
{
    lpDP->Close();
}

bool JDPlay::dpOpen(int maxPlayers, const char* sessionName, const char* mapName, std::uint32_t dwUser1, std::uint32_t dwUser2, std::uint32_t dwUser3, std::uint32_t dwUser4)
{
    std::ostringstream ss;
    ss << std::left << std::setfill(' ') << std::setw(16) << sessionName << mapName;
    std::string sessionAndMap = FromAnsi(ss.str().c_str());

    dpSessionDesc.dwCurrentPlayers = 0;
    dpSessionDesc.dwMaxPlayers = maxPlayers;
    dpSessionDesc.lpszSessionName = (LPWSTR)sessionAndMap.data();
    dpSessionDesc.dwUser1 = dwUser1;
    dpSessionDesc.dwUser2 = dwUser2;
    dpSessionDesc.dwUser3 = dwUser3;
    dpSessionDesc.dwUser4 = dwUser4;
    return lpDP->Open(&dpSessionDesc, DPOPEN_CREATE) == S_OK;
}

void JDPlay::dpGetSession(std::uint32_t& dwUser1, std::uint32_t& dwUser2, std::uint32_t& dwUser3, std::uint32_t& dwUser4)
{
    DWORD size;
    lpDP->GetSessionDesc(NULL, &size);
    lpDP->GetSessionDesc(&dpSessionDesc, &size);
    dwUser1 = dpSessionDesc.dwUser1;
    dwUser2 = dpSessionDesc.dwUser2;
    dwUser3 = dpSessionDesc.dwUser3;
    dwUser4 = dpSessionDesc.dwUser4;
}

void JDPlay::dpSetSession(std::uint32_t dwUser1, std::uint32_t dwUser2, std::uint32_t dwUser3, std::uint32_t dwUser4)
{
    dpSessionDesc.dwUser1 = dwUser1;
    dpSessionDesc.dwUser2 = dwUser2;
    dpSessionDesc.dwUser3 = dwUser3;
    dpSessionDesc.dwUser4 = dwUser4;
    lpDP->SetSessionDesc(&dpSessionDesc, dpSessionDesc.dwSize);
}

void JDPlay::dpSend(DPID sourceDplayId, DPID destDplayId, DWORD flags, LPVOID data, DWORD size)
{
    lpDP->Send(sourceDplayId, destDplayId, flags, data, size);
}

bool JDPlay::dpReceive(std::uint8_t *buffer, std::uint32_t &size, std::uint32_t &from, std::uint32_t &to)
{
    DPID _from = from, _to = to;
    DWORD _size;
    _size = size;
    HRESULT hr = lpDP->Receive(&_from, &_to, 1, buffer, &_size);
    size = _size;
    from = _from;
    to = _to;
    return hr == S_OK;
}

std::uint32_t JDPlay::dpCreatePlayer(const char *nameA)
{
    DPID dpid;
    DPNAME dpname;
    std::string name = FromAnsi(nameA);
    std::memset(&dpname, 0, sizeof(dpname));
    dpname.dwSize = sizeof(dpname);
    dpname.lpszLongName = (LPWSTR)name.data();
    dpname.lpszShortName = (LPWSTR)name.data();
    
    lpDP->CreatePlayer(&dpid, &dpname, 0, NULL, 0, 0);
    return dpid;
}

void JDPlay::dpDestroyPlayer(std::uint32_t dpid)
{
    lpDP->DestroyPlayer(dpid);
}

void JDPlay::dpSetPlayerName(std::uint32_t id, const char* nameA)
{
    DPNAME dpname;
    std::string name = FromAnsi(nameA);
    dpname.dwFlags = 0u;
    dpname.dwSize = sizeof(dpname);
    dpname.lpszLongName = (LPWSTR)name.data();
    dpname.lpszShortName = (LPWSTR)name.data();
    lpDP->SetPlayerName(id, &dpname, DPSET_GUARANTEED);
}

void JDPlay::dpEnumPlayers()
{
    lpDP->EnumPlayers(&dpSessionDesc.guidInstance, enumPlayersCallback, NULL, 0);
}

JDPlay* JDPlay::getInstance(){
	return instance;
}


void JDPlay::releaseDirectPlay()
{
    HRESULT hr;

    if (lpDP){

        if (lpDPIsOpen){
            hr = lpDP->Close();		//close dplay interface
            if (hr != S_OK){
                if (debug){
                    cout << "deInitialize() - ERROR[" << getDPERR(hr) << "]: failed to close DirectPlay interface" << endl;
                    fflush(stdout);
                }
            }
            lpDPIsOpen = false;
        }

        hr = lpDP->Release();	//release dplay interface
        if (hr != S_OK){
            if (debug){
                cout << "deInitialize() - ERROR[" << getDPERR(hr) << "]: failed to release DirectPlay interface" << endl;
                fflush(stdout);
            }
        }

        lpDP = NULL;  // set to NULL, safe practice here

        if (debug){
            cout << "deInitialize() - DirectPlay deinitialized" << endl;
            fflush(stdout);
        }
    }
}

void JDPlay::releaseLobby()
{
    HRESULT hr;

    if (lpDPLobby){

        hr = lpDPLobby->Release(); //release lobby
        if (hr != S_OK){
            if (debug){
                cout << "deInitialize() - ERROR[" << getDPERR(hr) << "]: failed to release lobby interface" << endl;
                fflush(stdout);
            }
        }
        lpDPLobby = NULL;

        if (debug){
            cout << "deInitialize() - lobby deinitialized" << endl;
            fflush(stdout);
        }
    }
}

void JDPlay::deInitialize(){
	if(debug){
		cout << "++ deInitialize()" << endl;
		fflush(stdout);
	}

    releaseDirectPlay();
    releaseLobby();

	if(debug){
		cout << "-- deInitialize()" << endl;
		fflush(stdout);
	}

	isInitialized = false;
}

const char* JDPlay::getDPERR(HRESULT hr){
	if(DP_OK) return "DP_OK";
	if(DPERR_ALREADYINITIALIZED) return "DPERR_ALREADYINITIALIZED";
	if(DPERR_ACCESSDENIED) return "DPERR_ACCESSDENIED";
	if(DPERR_ACTIVEPLAYERS) return "DPERR_ACTIVEPLAYERS";
	if(DPERR_BUFFERTOOSMALL) return "DPERR_BUFFERTOOSMALL";
	if(DPERR_CANTADDPLAYER) return "DPERR_CANTADDPLAYER";
	if(DPERR_CANTCREATEGROUP) return "DPERR_CANTCREATEGROUP";
	if(DPERR_CANTCREATEPLAYER) return "DPERR_CANTCREATEPLAYER";
	if(DPERR_CANTCREATESESSION) return "DPERR_CANTCREATESESSION";
	if(DPERR_CAPSNOTAVAILABLEYET) return "DPERR_CAPSNOTAVAILABLEYET";
	if(DPERR_EXCEPTION) return "DPERR_CAPSNOTAVAILABLEYET";
	if(DPERR_GENERIC) return "DPERR_GENERIC";
	if(DPERR_INVALIDFLAGS) return "DPERR_INVALIDFLAGS";
	if(DPERR_INVALIDOBJECT) return "DPERR_INVALIDOBJECT";
	if(DPERR_INVALIDPARAMS) return "DPERR_INVALIDPARAMS";
	if(DPERR_INVALIDPLAYER) return "DPERR_INVALIDPLAYER";
	if(DPERR_INVALIDGROUP) return "DPERR_INVALIDGROUP";
	if(DPERR_NOCAPS) return "DPERR_NOCAPS";
	if(DPERR_NOCONNECTION) return "DPERR_NOCONNECTION";
	if(DPERR_NOMEMORY) return "DPERR_NOMEMORY";
	if(DPERR_OUTOFMEMORY) return "DPERR_OUTOFMEMORY";
	if(DPERR_NOMESSAGES) return "DPERR_NOMESSAGES";
	if(DPERR_NONAMESERVERFOUND) return "DPERR_NONAMESERVERFOUND";
	if(DPERR_NOPLAYERS) return "DPERR_NOPLAYERS";
	if(DPERR_NOSESSIONS) return "DPERR_NOSESSIONS";
	if(DPERR_PENDING) return "DPERR_PENDING";
	if(DPERR_SENDTOOBIG) return "DPERR_SENDTOOBIG";
	if(DPERR_TIMEOUT) return "DPERR_TIMEOUT";
	if(DPERR_UNAVAILABLE) return "DPERR_UNAVAILABLE";
	if(DPERR_UNSUPPORTED) return "DPERR_UNSUPPORTED";
	if(DPERR_BUSY) return "DPERR_BUSY";
	if(DPERR_USERCANCEL) return "DPERR_USERCANCEL";
	if(DPERR_NOINTERFACE) return "DPERR_NOINTERFACE";
	if(DPERR_CANNOTCREATESERVER) return "DPERR_CANNOTCREATESERVER";
	if(DPERR_PLAYERLOST) return "DPERR_PLAYERLOST";
	if(DPERR_SESSIONLOST) return "DPERR_SESSIONLOST";
	if(DPERR_UNINITIALIZED) return "DPERR_UNINITIALIZED";
	if(DPERR_NONEWPLAYERS) return "DPERR_NONEWPLAYERS";
	if(DPERR_INVALIDPASSWORD) return "DPERR_INVALIDPASSWORD";
	if(DPERR_CONNECTING) return "DPERR_CONNECTING";
	if(DPERR_CONNECTIONLOST) return "DPERR_CONNECTIONLOST";
	if(DPERR_UNKNOWNMESSAGE) return "DPERR_UNKNOWNMESSAGE";
	if(DPERR_CANCELFAILED) return "DPERR_CANCELFAILED";
	if(DPERR_INVALIDPRIORITY) return "DPERR_INVALIDPRIORITY";
	if(DPERR_NOTHANDLED) return "DPERR_NOTHANDLED";
	if(DPERR_CANCELLED) return "DPERR_CANCELLED";
	if(DPERR_ABORTED) return "DPERR_ABORTED";
	if(DPERR_BUFFERTOOLARGE) return "DPERR_BUFFERTOOLARGE";
	if(DPERR_CANTCREATEPROCESS) return "DPERR_CANTCREATEPROCESS";
	if(DPERR_APPNOTSTARTED) return "DPERR_APPNOTSTARTED";
	if(DPERR_INVALIDINTERFACE) return "DPERR_INVALIDINTERFACE";
	if(DPERR_NOSERVICEPROVIDER) return "DPERR_NOSERVICEPROVIDER";
	if(DPERR_UNKNOWNAPPLICATION) return "DPERR_UNKNOWNAPPLICATION";
	if(DPERR_NOTLOBBIED) return "DPERR_NOTLOBBIED";
	if(DPERR_SERVICEPROVIDERLOADED) return "DPERR_SERVICEPROVIDERLOADED";
	if(DPERR_ALREADYREGISTERED) return "DPERR_ALREADYREGISTERED";
	if(DPERR_NOTREGISTERED) return "DPERR_NOTREGISTERED";
	if(DPERR_AUTHENTICATIONFAILED) return "DPERR_AUTHENTICATIONFAILED";
	if(DPERR_CANTLOADSSPI) return "DPERR_CANTLOADSSPI";
	if(DPERR_ENCRYPTIONFAILED) return "DPERR_ENCRYPTIONFAILED";
	if(DPERR_SIGNFAILED) return "DPERR_SIGNFAILED";
	if(DPERR_CANTLOADSECURITYPACKAGE) return "DPERR_CANTLOADSECURITYPACKAGE";
	if(DPERR_ENCRYPTIONNOTSUPPORTED) return "DPERR_ENCRYPTIONNOTSUPPORTED";
	if(DPERR_CANTLOADCAPI) return "DPERR_CANTLOADCAPI";
	if(DPERR_NOTLOGGEDIN) return "DPERR_NOTLOGGEDIN";
	if(DPERR_LOGONDENIED) return "DPERR_LOGONDENIED";

	return "ERROR";
}
