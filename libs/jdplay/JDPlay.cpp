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
#include <cctype>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include "DPlayWrapper.h"

using namespace std;

#define SET_LAST_ERROR(msg) { \
    std::ostringstream ss; \
    ss << msg; \
    lastError = ss.str(); \
}

//#define GLOBAL_DEBUG
#ifdef GLOBAL_DEBUG
static std::ofstream GlobalDebug("c:\\temp\\jdplay.log");
#endif

static std::ostream& operator<< (std::ostream& os, const GUID& guid)
{
    const unsigned char* ptr = (const unsigned char*)&guid;
    static const int idx[sizeof(GUID)] = { 3,2,1,0, 5,4, 7,6, 8,9, 10,11,12,13,14,15 };
    ios init(NULL);
    init.copyfmt(os);

    os << '{';
    for (int n = 0; n < sizeof(GUID); ++n)
    {
        os << std::hex << std::setw(2) << std::setfill('0') << unsigned(ptr[idx[n]]);
        if (n == 3 || n == 5 || n == 7 || n == 9) {
            os << '-';
        }
    }
    os << '}';

    os.copyfmt(init);
    return os;
}

static std::string DPCONNECTION_dwFlags_to_str(DWORD dwFlags)
{
    switch (dwFlags)
    {
    case DPLCONNECTION_CREATESESSION: return "DPOPEN_CREATE";
    case DPLCONNECTION_JOINSESSION: return "DPOPEN_JOIN";
    default:
    {
        std::ostringstream ss;
        ss << dwFlags;
        return ss.str();
    }
    };
}

struct HexBytes
{
    const unsigned char* m_data;
    int m_size;
    HexBytes(const void* data, int size) : m_data((const unsigned char*)data), m_size(size) {}
};

struct CharBytes
{
    const char* m_data;
    int m_size;
    CharBytes(const void* data, int size) : m_data((const char*)data), m_size(size) {}
};

static std::ostream& operator<<(std::ostream &os, const HexBytes &hb)
{
    if (hb.m_data == NULL)
    {
        os << "null";
        return os;
    }

    ios init(NULL);
    init.copyfmt(os);
    os << std::hex << std::setw(2) << std::setfill('0');
    for (int n = 0; n < hb.m_size; ++n)
    {
        os << unsigned(hb.m_data[n]);
    }
    os.copyfmt(init);
    return os;
}

static std::ostream& operator<<(std::ostream& os, const CharBytes &b)
{
    if (b.m_data == NULL)
    {
        os << "null";
        return os;
    }

    for (int n = 0; n < b.m_size; ++n)
    {
        if (std::isprint(b.m_data[n]))
        {
            os << b.m_data[n];
        }
        else
        {
            os << '.';
        }
    }
    return os;
}

static int safewcslen(const wchar_t* ws)
{
    if (ws)
    {
        return wcslen(ws);
    }
    return 0;
}

static std::ostream& operator<< (std::ostream& os, const DPNAME& name)
{
    os << "{lpszShortName='" << CharBytes(name.lpszShortName, 2 * safewcslen(name.lpszShortName)) << "'"
        << ", lpszLongName='" << CharBytes(name.lpszLongName, 2 * safewcslen(name.lpszLongName)) << "'}";
    return os;
}

static std::ostream &print(std::ostream& os, const DPSESSIONDESC2* desc, int indent)
{
    if (desc == NULL)
    {
        os << "NULL";
        return os;
    }

    std::string margin(indent, ' ');
    ios init(NULL);
    init.copyfmt(os);
    os << "{" << std::endl << margin;
    os << "dwFlags=" << std::hex << desc->dwFlags << "," << std::endl << margin;
    os << "guidInstance=" << desc->guidInstance << ", guidApplication=" << desc->guidApplication << "," << std::endl << margin;
    os << "dwMaxPlayers=" << std::dec << desc->dwMaxPlayers << ", dwCurrentPlayers=" << desc->dwCurrentPlayers << "," << std::endl << margin;
    os << "dwUser1=" << std::hex << desc->dwUser1 << ", dwUser2=" << desc->dwUser2 << ", dwUser3=" << desc->dwUser3 << ", dwUser4=" << desc->dwUser4 << "," << std::endl << margin;
    os << "lpszSessionName=" << CharBytes(desc->lpszSessionName, 2 * safewcslen(desc->lpszSessionName)) << "," << std::endl << margin;
    os << "lpszPassword=" << CharBytes(desc->lpszPassword, 2 * safewcslen(desc->lpszPassword)) << std::endl << margin << "}";
    os.copyfmt(init);
    return os;
}

static std::ostream& print(std::ostream& os, const DPLCONNECTION* conn, int indent)
{
    if (conn == NULL)
    {
        os << "NULL";
        return os;
    }

    std::string margin(indent, ' ');
    os << "{" << std::endl << margin
        << "dwFlags=" << DPCONNECTION_dwFlags_to_str(conn->dwFlags) << "," << std::endl << margin
        << "guidSP=" << conn->guidSP << "," << std::endl << margin
        << "address=" << CharBytes(conn->lpAddress, conn->dwAddressSize) << "," << std::endl << margin
        << "address(hex)=" << HexBytes(conn->lpAddress, conn->dwAddressSize) << "," << std::endl << margin;
        os << "lpSessionDesc="; print(os, conn->lpSessionDesc, indent+4) << "," << std::endl << margin;

    if (conn->lpPlayerName)
    {
        os << "lpPlayerName=" << *conn->lpPlayerName << std::endl << margin;
    }
    else
    {
        os << "lpPlayerName=NULL" << std::endl << margin;
    }
    os << "}";

    return os;
}

namespace jdplay {

    inline BSTR _ConvertStringToBSTR(const char* pSrc)
    {
        if (!pSrc) return NULL;

        DWORD cwch;

        BSTR wsOut(NULL);

        if (cwch = ::MultiByteToWideChar(CP_ACP, 0, pSrc,
            -1, NULL, 0))//get size minus NULL terminator
        {
            cwch--;
            wsOut = ::SysAllocStringLen(NULL, cwch);

            if (wsOut)
            {
                if (!::MultiByteToWideChar(CP_ACP,
                    0, pSrc, -1, wsOut, cwch))
                {
                    if (ERROR_INSUFFICIENT_BUFFER == ::GetLastError())
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
        for (; *ansi != 0u && maxBytes > 3u; ++ansi, ++dest, maxBytes -= 2)
        {
            *dest = *ansi;
        }
        *dest = 0;
    }

    JDPlay* JDPlay::instance;

    BOOL FAR PASCAL EnumSessionsCallback(LPCDPSESSIONDESC2 lpThisSD, LPDWORD lpdwTimeOut, DWORD dwFlags, LPVOID lpContext) {
        if (lpThisSD) {
            JDPlay::getInstance()->updateFoundSessionDescription(lpThisSD);
        }
        return false;
    }

    BOOL FAR PASCAL enumPlayersCallback(
        DPID            dpId,
        DWORD           dwPlayerType,
        LPCDPNAME       lpName,
        DWORD           dwFlags,
        LPVOID          lpContext)
    {
        //cout << "\rdpID:" << dpId
        //    << ", dwPlayerType:" << (dwPlayerType == DPPLAYERTYPE_PLAYER ? "Player" : "Group")
        //    << ", lpName:" << ToAnsi(lpName->lpszShortName);
        //if (dwFlags & DPENUMGROUPS_SHORTCUT) cout << ", SHORTCUT";
        //if (dwFlags & DPENUMGROUPS_STAGINGAREA) cout << ", STAGING";
        //if (dwFlags & DPENUMPLAYERS_GROUP) cout << ", PLYRS&GRPS";
        //if (dwFlags & DPENUMPLAYERS_LOCAL) cout << ", LOCAL";
        //if (dwFlags & DPENUMPLAYERS_REMOTE) cout << ", REMOTE";
        //if (dwFlags & DPENUMPLAYERS_SESSION) cout << ", SESSION";
        //if (dwFlags & DPENUMPLAYERS_SERVERPLAYER) cout << ", SERVER";
        //if (dwFlags & DPENUMPLAYERS_SPECTATOR) cout << ", SPECTATOR";
        //cout << std::endl;
        return true;
    }

    std::ostream& JDPlay::debug()
    {
        if (m_debugStream)
        {
            return *m_debugStream;
        }
        else
        {
            return m_logStream;
        }
    }

    JDPlay::JDPlay(const char* playerName, int searchValidationCount, const char *debugOutputFile)
    {
        if (debugOutputFile) {
            m_debugStream.reset(new std::ofstream(debugOutputFile));
            debug() << "++ JDPlay(" << playerName << "," << searchValidationCount << "," << debugOutputFile << ")" << endl;
        }

        this->instance = this;
        this->searchValidationCount = searchValidationCount;
        this->validateCount = -1;
        this->lpDPIsOpen = false;
        this->isInitialized = false;

        // clear out memory for info objects
        ZeroMemory(&dpName, sizeof(DPNAME));
        ZeroMemory(&dpSessionDesc, sizeof(DPSESSIONDESC2));
        ZeroMemory(&dpConn, sizeof(DPLCONNECTION));

        // populate player info
        dpName.dwSize = sizeof(DPNAME);
        dpName.dwFlags = 0; // not used, must be zero
        dpName.lpszLongName = (LPWSTR)malloc(256);
        dpName.lpszShortName = (LPWSTR)malloc(256);
        updatePlayerName(playerName);

        debug() << "-- JDPlay()" << endl;
    }

    JDPlay::~JDPlay() {

        debug() << "++ ~JDPlay()" << endl;
        if (isInitialized) {
            deInitialize();
        }
        debug() << "-- ~JDPlay()" << endl;
    }

    std::string JDPlay::getLastError()
    {
        return lastError;
    }

    std::string JDPlay::getLogString()
    {
        std::string s = m_logStream.str();
        while (s.find('\0') != std::string::npos)
        {
            s[s.find('\0')] = '@';
        }
        return s;
    }

    bool JDPlay::initialize(const char* gameGUID, const char* hostIP, bool isHost, int maxPlayers) {

        debug() << "++ initialize(" << gameGUID << ", " << hostIP << ", " << isHost << ")" << endl;

        if (isInitialized) {
            deInitialize();
        }

        HRESULT hr;

        // create GUID Object **************************************************************************
        GUID gameID;

        LPOLESTR lpoleguid = _ConvertStringToBSTR(gameGUID);
        hr = CLSIDFromString(lpoleguid, &gameID);
        ::SysFreeString(lpoleguid);

        if (hr != S_OK) {
            SET_LAST_ERROR("initialize() - ERROR: invalid GUID");
            return false;
        }

        debug() << "initialize() - GUID initialized" << endl;

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
                SET_LAST_ERROR("initialize() - ERROR: failed to initialize COM");
                return false;
            }

            debug() << "initialize() - COM initialized" << endl;

            // creating directplay object
            hr = CoCreateInstance(CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay3, (LPVOID*)&lpDP);

            if (hr != S_OK) {
                SET_LAST_ERROR("initialize() - ERROR: failed to initialize DirectPlay");
                return false;
            }

            CoUninitialize();  // unregister the COM

            // creating lobby object
            hr = DirectPlayLobbyCreate(NULL, &old_lpDPLobby, NULL, NULL, 0);

            if (hr != S_OK) {
                SET_LAST_ERROR("initialize() - ERROR[" << getDPERR(hr) << "]: failed to create lobby object");
                return false;
            }

            // get new interface of lobby
            hr = old_lpDPLobby->QueryInterface(IID_IDirectPlayLobby3, (LPVOID*)&lpDPLobby);

            if (hr != S_OK) {
                SET_LAST_ERROR("initialize() - ERROR[" << getDPERR(hr) << "]: failed to get new lobby interface");
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
                SET_LAST_ERROR(std::hex << hr << " unable to create DirectPlay");
                return false;
            }
            hr = dp1->QueryInterface(IID_IDirectPlay3, (LPVOID*)&lpDP);
            if (hr != S_OK) {
                SET_LAST_ERROR(std::hex << hr << " unable to get DirectPlay3");
                return false;
            }
            hr = DPlayWrapper().directPlayLobbyCreate(NULL, &old_lpDPLobby, NULL, NULL, 0);
            if (hr != S_OK) {
                SET_LAST_ERROR(std::hex << hr << " unable to create DirectPlayLobby");
                return false;
            }
            hr = old_lpDPLobby->QueryInterface(IID_IDirectPlayLobby3, (LPVOID*)&lpDPLobby);
            if (hr != S_OK) {
                SET_LAST_ERROR(std::hex << hr << " unable to get DirectPlayLobby3");
                return false;
            }
        }

        // release old interface since we have new one
        hr = old_lpDPLobby->Release();

        if (hr != S_OK) {
            SET_LAST_ERROR("initialize() - ERROR[" << getDPERR(hr) << "]: failed to release old lobby interface");
            return false;
        }

        // fill in data for address
        address[0].guidDataType = DPAID_ServiceProvider;
        address[0].dwDataSize = sizeof(GUID);
        address[0].lpData = (LPVOID)&DPSPGUID_TCPIP;  // TCP ID

        if (isHost) { //Bind to any ip?
            hostIP = "";
        }

        address[1].guidDataType = DPAID_INet;
        address[1].dwDataSize = static_cast<DWORD>(strlen(hostIP) + 1);
        address[1].lpData = const_cast<char*>(hostIP);

        // get size to create address
        // this method will return DPERR_BUFFERTOOSMALL, that is not an error
        hr = lpDPLobby->CreateCompoundAddress(address, 2, NULL, &addressSize);

        if (addressSize == 0) { //hr != S_OK && hr != DPERR_BUFFERTOOSMALL) {
            SET_LAST_ERROR("initialize() - ERROR[" << getDPERR(hr) << "]: failed to get size for CompoundAddress. addressSize=" << addressSize);
            addressSize = 1000;
            //return false;
        }

        lpConnection = GlobalAllocPtr(GHND, addressSize);  // allocating mem

        // now creating the address
        hr = lpDPLobby->CreateCompoundAddress(address, 2, lpConnection, &addressSize);

        if (hr != S_OK) {
            SET_LAST_ERROR("initialize() - ERROR[" << getDPERR(hr) << "]: failed to create CompoundAddress");
            return false;
        }

        // initialize the tcp connection
        hr = lpDP->InitializeConnection(lpConnection, 0);

        if (hr != S_OK) {
            SET_LAST_ERROR("initialize() - ERROR[" << getDPERR(hr) << "]: failed to initialize TCP connection");
            return false;
        }

        // populate session description ******************************************************************
        dpSessionNameStorage = L"TAF Game";
        dpSessionPasswordStorage = L"";
        dpSessionDesc.dwSize = sizeof(DPSESSIONDESC2);
        dpSessionDesc.dwFlags = 0;									// optional: DPSESSION_MIGRATEHOST
        dpSessionDesc.guidApplication = gameID;						// Game GUID
        dpSessionDesc.guidInstance = gameID;						// ID for the session instance
        dpSessionDesc.lpszSessionName = (wchar_t *)dpSessionNameStorage.c_str();
        dpSessionDesc.dwMaxPlayers = maxPlayers;					// Maximum # players allowed in session
        dpSessionDesc.dwCurrentPlayers = 0;							// Current # players in session (read only)
        dpSessionDesc.lpszPassword = (wchar_t*)dpSessionPasswordStorage.c_str();
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
        if (isHost) {
            dpConn.dwFlags = DPLCONNECTION_CREATESESSION;
        }
        else {
            dpConn.dwFlags = DPLCONNECTION_JOINSESSION;
        }

        // set other vars
        if (isHost) {
            sessionFlags = DPOPEN_CREATE;
            playerFlags = DPPLAYER_SERVERPLAYER;
        }
        else {
            sessionFlags = DPOPEN_JOIN;
            playerFlags = 0;
        }

        isInitialized = true;
        return true;
    }

    bool JDPlay::isHost() {
        return sessionFlags != DPOPEN_JOIN;
    }

    void JDPlay::updatePlayerName(const char* playerNameA) {
        FromAnsi(playerNameA, dpName.lpszShortName, 256);
        FromAnsi(playerNameA, dpName.lpszLongName, 256);
    }
    bool JDPlay::searchOnce() {
        HRESULT hr;

        if (!isHost()) {

            foundLobby = false;

            if (lpDP) {

                hr = lpDP->EnumSessions(&dpSessionDesc, 0, EnumSessionsCallback, NULL, 0);
                if (hr != S_OK) {
                    SET_LAST_ERROR("searchOnce() - ERROR[" << getDPERR(hr) << "]: failed to enumerate sessions");
                    return false;
                }

                if (!foundLobby) {
                    SET_LAST_ERROR("searchOnce() - no session found");
                    return false;
                }
            }

            if (lpDP) {

                hr = lpDP->Open(&dpSessionDesc, sessionFlags | DPOPEN_RETURNSTATUS);
                if (hr != S_OK) {
                    SET_LAST_ERROR("searchOnce() - ERROR[" << getDPERR(hr) << "]: failed to open DirectPlay session");
                    return false;
                }

                lpDPIsOpen = true;
            }
        }
        else {
            // searchOnce() - skipping session search, not needed because hosting
        }

        return true;
    }

    bool JDPlay::launch(bool startGame) {
        std::time_t now = std::time(0);
        char* date = std::ctime(&now);
        debug() << "[JDPlay::launch] " << date << ", startGame=" << startGame << std::endl;

        if (!isInitialized) {
            SET_LAST_ERROR("launch() - WARNING: JDPlay has to be initialized before launching!");
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
        debug() << "dpConn="; print(debug(), &dpConn, 4) << std::endl;
        hr = lpDPLobby->RunApplication(0, &appID, &dpConn, 0);
        debug() << "RunApplication returned hr=" << hr << ", appId=" << appID << std::endl;

        if (hr != S_OK) {
            SET_LAST_ERROR("launch() - ERROR[" << getDPERR(hr) << "]: failed to launch the game, maybe it's not installed properly");
            return false;
        }

        // wait until game exits ***********************************************************************
        HANDLE appHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, appID);
        if (appHandle == NULL) {
            SET_LAST_ERROR("launch() - ERROR: failed to open game process");
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

        SET_LAST_ERROR("");
        if (dplay)
        {
            hr = dplay->EnumSessions(&dpSessionDesc, 0, &EnumSessionsCallback, NULL, 0);
            if (S_OK != hr)
            {
                SET_LAST_ERROR("EnumSessions:" << getDPERR(hr));
            }
            //hr = lpDP->EnumGroups(&dpSessionDesc.guidInstance, enumPlayersCallback, NULL, 0);
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
                SET_LAST_ERROR("CoInitialize:" << getDPERR(hr));
            }
            hr = CoCreateInstance(CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, IID_IDirectPlay3, (LPVOID*)&dplay);
            CoUninitialize();  // unregister the COM
            if (S_OK != hr)
            {
                SET_LAST_ERROR(getLastError() << "\nCoCreateInstance:" << getDPERR(hr));
            }
            pollSessionStatus(dplay);
            dplay->Close();
            dplay->Release();
        }
    }

    void JDPlay::updateFoundSessionDescription(LPCDPSESSIONDESC2 lpFoundSD) {
        std::time_t now = std::time(0);
        char* date = std::ctime(&now);

#ifdef GLOBAL_DEBUG
        if (lpFoundSD) {
            const DPSESSIONDESC2& dpSessionDesc = *lpFoundSD;
            GlobalDebug << "[JDPlay::updateFoundSessionDescription] "
                << " dwFlags:" << std::hex << dpSessionDesc.dwFlags
                << " guidInstance:" << GuidToString(dpSessionDesc.guidInstance)
                << " guidApplication:" << GuidToString(dpSessionDesc.guidApplication)
                << " dwMaxPlayers:" << dpSessionDesc.dwMaxPlayers
                << " dwCurrentPlayers:" << dpSessionDesc.dwCurrentPlayers
                << " dwReserved1:" << std::hex << dpSessionDesc.dwReserved1
                << " dwReserved2:" << std::hex << dpSessionDesc.dwReserved2
                << " dwUser1:" << std::hex << dpSessionDesc.dwUser1
                << " dwUser2:" << std::hex << dpSessionDesc.dwUser2
                << " dwUser3:" << std::hex << dpSessionDesc.dwUser3
                << " dwUser4:" << std::hex << dpSessionDesc.dwUser4
                << std::endl;
            GlobalDebug.flush();
        }
        else {
            GlobalDebug << "[JDPlay::updateFoundSessionDescription] NULL lpFoundSD!";
        }
#endif

        debug()
            << "[JDPlay::updateFoundSessionDescription] " << date
            << "validateCount=" << validateCount
            << ", searchValidationCount=" << searchValidationCount << std::endl;
        debug() << "lpFoundSD="; print(debug(), lpFoundSD, 4) << std::endl;

        if (validateCount > -1) {
            debug() << "validateCount > -1" << std::endl;
            bool areEqual = dpSessionDesc.dwSize == lpFoundSD->dwSize
                && dpSessionDesc.dwFlags == lpFoundSD->dwFlags
                && dpSessionDesc.guidInstance == lpFoundSD->guidInstance
                && dpSessionDesc.guidApplication == lpFoundSD->guidApplication
                && dpSessionDesc.dwMaxPlayers == lpFoundSD->dwMaxPlayers
                && dpSessionDesc.dwReserved1 == lpFoundSD->dwReserved1
                && dpSessionDesc.dwReserved2 == lpFoundSD->dwReserved2
                && dpSessionDesc.dwUser1 == lpFoundSD->dwUser1
                && dpSessionDesc.dwUser2 == lpFoundSD->dwUser2
                && dpSessionDesc.dwUser3 == lpFoundSD->dwUser3
                && dpSessionDesc.dwUser4 == lpFoundSD->dwUser4;

            if (areEqual) {
                validateCount++;
                debug() << "areEqual" << std::endl;
            }
            else {
                validateCount = -1;
                debug() << "!areEqual" << std::endl;
            }
        }
        else {
            debug() << "validateCount <= -1" << std::endl;
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

            dpSessionNameStorage = L"TAF Game";
            if (lpFoundSD->lpszSessionName)
            {
                dpSessionNameStorage = lpFoundSD->lpszSessionName;
            }
            dpSessionDesc.lpszSessionName = (wchar_t*)dpSessionNameStorage.c_str();

            dpSessionPasswordStorage = L"";
            if (lpFoundSD->lpszPassword)
            {
                dpSessionPasswordStorage = lpFoundSD->lpszPassword;
            }
            dpSessionDesc.lpszPassword = (wchar_t*)dpSessionPasswordStorage.c_str();

            validateCount = 0;
        }

        foundLobby = (validateCount >= searchValidationCount);
        debug() << "foundLobby=" << foundLobby << std::endl;
    }

    std::wstring JDPlay::getAdvertisedPlayerName()
    {
        return dpName.lpszShortName;
    }

    std::wstring JDPlay::getAdvertisedSessionName()
    {
        return dpSessionNameStorage;
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

    bool JDPlay::dpReceive(std::uint8_t* buffer, std::uint32_t& size, std::uint32_t& from, std::uint32_t& to)
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

    std::uint32_t JDPlay::dpCreatePlayer(const char* nameA)
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

    JDPlay* JDPlay::getInstance() {
        return instance;
    }


    void JDPlay::releaseDirectPlay()
    {
        HRESULT hr;

        SET_LAST_ERROR("");
        if (lpDP) {
            if (lpDPIsOpen) {
                hr = lpDP->Close();		//close dplay interface
                if (hr != S_OK) {
                    SET_LAST_ERROR("deInitialize() - ERROR[" << getDPERR(hr) << "]: failed to close DirectPlay interface");
                }
                lpDPIsOpen = false;
            }

            hr = lpDP->Release();	//release dplay interface
            if (hr != S_OK) {
                SET_LAST_ERROR(getLastError() << "\ndeInitialize() - ERROR[" << getDPERR(hr) << "]: failed to release DirectPlay interface");
            }

            lpDP = NULL;  // set to NULL, safe practice here
        }
    }

    void JDPlay::releaseLobby()
    {
        HRESULT hr;

        SET_LAST_ERROR("");
        if (lpDPLobby) {

            hr = lpDPLobby->Release(); //release lobby
            if (hr != S_OK) {
                SET_LAST_ERROR("deInitialize() - ERROR[" << getDPERR(hr) << "]: failed to release lobby interface");
            }
            lpDPLobby = NULL;
        }
    }

    void JDPlay::deInitialize() {
        releaseDirectPlay();
        releaseLobby();
        isInitialized = false;
    }

    const char* JDPlay::getDPERR(HRESULT hr) {
        if (DP_OK) return "DP_OK";
        if (DPERR_ALREADYINITIALIZED) return "DPERR_ALREADYINITIALIZED";
        if (DPERR_ACCESSDENIED) return "DPERR_ACCESSDENIED";
        if (DPERR_ACTIVEPLAYERS) return "DPERR_ACTIVEPLAYERS";
        if (DPERR_BUFFERTOOSMALL) return "DPERR_BUFFERTOOSMALL";
        if (DPERR_CANTADDPLAYER) return "DPERR_CANTADDPLAYER";
        if (DPERR_CANTCREATEGROUP) return "DPERR_CANTCREATEGROUP";
        if (DPERR_CANTCREATEPLAYER) return "DPERR_CANTCREATEPLAYER";
        if (DPERR_CANTCREATESESSION) return "DPERR_CANTCREATESESSION";
        if (DPERR_CAPSNOTAVAILABLEYET) return "DPERR_CAPSNOTAVAILABLEYET";
        if (DPERR_EXCEPTION) return "DPERR_CAPSNOTAVAILABLEYET";
        if (DPERR_GENERIC) return "DPERR_GENERIC";
        if (DPERR_INVALIDFLAGS) return "DPERR_INVALIDFLAGS";
        if (DPERR_INVALIDOBJECT) return "DPERR_INVALIDOBJECT";
        if (DPERR_INVALIDPARAMS) return "DPERR_INVALIDPARAMS";
        if (DPERR_INVALIDPLAYER) return "DPERR_INVALIDPLAYER";
        if (DPERR_INVALIDGROUP) return "DPERR_INVALIDGROUP";
        if (DPERR_NOCAPS) return "DPERR_NOCAPS";
        if (DPERR_NOCONNECTION) return "DPERR_NOCONNECTION";
        if (DPERR_NOMEMORY) return "DPERR_NOMEMORY";
        if (DPERR_OUTOFMEMORY) return "DPERR_OUTOFMEMORY";
        if (DPERR_NOMESSAGES) return "DPERR_NOMESSAGES";
        if (DPERR_NONAMESERVERFOUND) return "DPERR_NONAMESERVERFOUND";
        if (DPERR_NOPLAYERS) return "DPERR_NOPLAYERS";
        if (DPERR_NOSESSIONS) return "DPERR_NOSESSIONS";
        if (DPERR_PENDING) return "DPERR_PENDING";
        if (DPERR_SENDTOOBIG) return "DPERR_SENDTOOBIG";
        if (DPERR_TIMEOUT) return "DPERR_TIMEOUT";
        if (DPERR_UNAVAILABLE) return "DPERR_UNAVAILABLE";
        if (DPERR_UNSUPPORTED) return "DPERR_UNSUPPORTED";
        if (DPERR_BUSY) return "DPERR_BUSY";
        if (DPERR_USERCANCEL) return "DPERR_USERCANCEL";
        if (DPERR_NOINTERFACE) return "DPERR_NOINTERFACE";
        if (DPERR_CANNOTCREATESERVER) return "DPERR_CANNOTCREATESERVER";
        if (DPERR_PLAYERLOST) return "DPERR_PLAYERLOST";
        if (DPERR_SESSIONLOST) return "DPERR_SESSIONLOST";
        if (DPERR_UNINITIALIZED) return "DPERR_UNINITIALIZED";
        if (DPERR_NONEWPLAYERS) return "DPERR_NONEWPLAYERS";
        if (DPERR_INVALIDPASSWORD) return "DPERR_INVALIDPASSWORD";
        if (DPERR_CONNECTING) return "DPERR_CONNECTING";
        if (DPERR_CONNECTIONLOST) return "DPERR_CONNECTIONLOST";
        if (DPERR_UNKNOWNMESSAGE) return "DPERR_UNKNOWNMESSAGE";
        if (DPERR_CANCELFAILED) return "DPERR_CANCELFAILED";
        if (DPERR_INVALIDPRIORITY) return "DPERR_INVALIDPRIORITY";
        if (DPERR_NOTHANDLED) return "DPERR_NOTHANDLED";
        if (DPERR_CANCELLED) return "DPERR_CANCELLED";
        if (DPERR_ABORTED) return "DPERR_ABORTED";
        if (DPERR_BUFFERTOOLARGE) return "DPERR_BUFFERTOOLARGE";
        if (DPERR_CANTCREATEPROCESS) return "DPERR_CANTCREATEPROCESS";
        if (DPERR_APPNOTSTARTED) return "DPERR_APPNOTSTARTED";
        if (DPERR_INVALIDINTERFACE) return "DPERR_INVALIDINTERFACE";
        if (DPERR_NOSERVICEPROVIDER) return "DPERR_NOSERVICEPROVIDER";
        if (DPERR_UNKNOWNAPPLICATION) return "DPERR_UNKNOWNAPPLICATION";
        if (DPERR_NOTLOBBIED) return "DPERR_NOTLOBBIED";
        if (DPERR_SERVICEPROVIDERLOADED) return "DPERR_SERVICEPROVIDERLOADED";
        if (DPERR_ALREADYREGISTERED) return "DPERR_ALREADYREGISTERED";
        if (DPERR_NOTREGISTERED) return "DPERR_NOTREGISTERED";
        if (DPERR_AUTHENTICATIONFAILED) return "DPERR_AUTHENTICATIONFAILED";
        if (DPERR_CANTLOADSSPI) return "DPERR_CANTLOADSSPI";
        if (DPERR_ENCRYPTIONFAILED) return "DPERR_ENCRYPTIONFAILED";
        if (DPERR_SIGNFAILED) return "DPERR_SIGNFAILED";
        if (DPERR_CANTLOADSECURITYPACKAGE) return "DPERR_CANTLOADSECURITYPACKAGE";
        if (DPERR_ENCRYPTIONNOTSUPPORTED) return "DPERR_ENCRYPTIONNOTSUPPORTED";
        if (DPERR_CANTLOADCAPI) return "DPERR_CANTLOADCAPI";
        if (DPERR_NOTLOGGEDIN) return "DPERR_NOTLOGGEDIN";
        if (DPERR_LOGONDENIED) return "DPERR_LOGONDENIED";

        return "ERROR";
    }

    std::string GuidToString(const GUID& guid) {
        std::ostringstream oss;
        oss << std::hex
            << std::setfill('0')
            << std::setw(8) << guid.Data1 << '-'
            << std::setw(4) << guid.Data2 << '-'
            << std::setw(4) << guid.Data3 << '-'
            << std::setw(2) << static_cast<int>(guid.Data4[0])
            << std::setw(2) << static_cast<int>(guid.Data4[1]) << '-'
            << std::setw(2) << static_cast<int>(guid.Data4[2])
            << std::setw(2) << static_cast<int>(guid.Data4[3]) << '-'
            << std::setw(2) << static_cast<int>(guid.Data4[4])
            << std::setw(2) << static_cast<int>(guid.Data4[5])
            << std::setw(2) << static_cast<int>(guid.Data4[6])
            << std::setw(2) << static_cast<int>(guid.Data4[7]);

        return oss.str();
    }

    DPSESSIONDESC2& JDPlay::enumSessions() {
        DPSESSIONDESC2 sessionDesc;
        ZeroMemory(&sessionDesc, sizeof(DPSESSIONDESC2));
        sessionDesc.dwSize = sizeof(DPSESSIONDESC2);
        sessionDesc.guidApplication = dpSessionDesc.guidApplication;
        sessionDesc.guidInstance = dpSessionDesc.guidInstance;
        HRESULT hr = lpDP->EnumSessions(&sessionDesc, 30, EnumSessionsCallback, 0, DPENUMSESSIONS_ALL);
#ifdef GLOBAL_DEBUG
        if FAILED(hr) {
            GlobalDebug << "[JDPlay::sessionDesc] FAILED to EnumSessions: " << getDPERR(hr) << std::endl;
        }
#endif GLOBAL_DEBUG
        return dpSessionDesc;
    }
}
