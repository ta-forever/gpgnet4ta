#pragma once
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

#include <cstdint>
#include <dplay.h>
#include <dplobby.h>
#include <memory>
#include <sstream>
#include <string>

namespace jdplay {

    BOOL FAR PASCAL EnumSessionsCallback(LPCDPSESSIONDESC2 lpThisSD, LPDWORD lpdwTimeOut, DWORD dwFlags, LPVOID lpContext);
    std::string GuidToString(const GUID& guid);

    class JDPlay {
    private:
        static JDPlay* instance;		// needed for callback function to access a method

        std::shared_ptr<std::ostream> m_debugStream;
        int curRetry;
        int searchValidationCount;
        int validateCount;
        bool foundLobby;
        bool isInitialized;
        bool lpDPIsOpen;
        std::wstring dpSessionNameStorage;
        std::wstring dpSessionPasswordStorage;
        std::string lastError;
        std::ostringstream m_logStream;

        LPDIRECTPLAY3 lpDP;		// directplay interface pointer
        LPDIRECTPLAYLOBBY3	lpDPLobby;	// lobby interface pointer

        DPNAME dpName;			// player description
        DPSESSIONDESC2 dpSessionDesc;       // session description
        DPLCONNECTION dpConn;		// connection description

        DPID dPid;			        // player ID (currently unused)
        DWORD appID;			// game process ID
        DWORD sessionFlags;                 // either Host or Join Session
        DWORD playerFlags;                  // either Host or not
        HANDLE processHandle;               // of game after launching

        std::ostream& debug();

    public:
        JDPlay(const char* playerName, int searchValidationCount, const char *debugOutputFile);
        ~JDPlay();

        std::string getLastError();
        std::string getLogString();
        void updatePlayerName(const char* playerName);
        bool initialize(const char* gameGUID, const char* hostIP, bool isHost, int maxPlayers);
        bool searchOnce();
        bool launch(bool startGame);
        bool pollStillActive(DWORD &exitCode);
        void pollSessionStatus(LPDIRECTPLAY3 dplay = NULL);
        bool isHost();
        void releaseDirectPlay();
        void releaseLobby();
        std::wstring getAdvertisedSessionName();
        std::wstring getAdvertisedPlayerName();
        DWORD_PTR getUserData1() { return dpSessionDesc.dwUser1; }
        DWORD_PTR getUserData2() { return dpSessionDesc.dwUser2; }
        DWORD_PTR getUserData3() { return dpSessionDesc.dwUser3; }
        DWORD_PTR getUserData4() { return dpSessionDesc.dwUser4; }

        void dpClose();
        bool dpOpen(int maxPlayers, const char* sessionName, const char* mapName, std::uint32_t dwUser1, std::uint32_t dwUser2, std::uint32_t dwUser3, std::uint32_t dwUser4);
        void dpGetSession(std::uint32_t&dwUser1, std::uint32_t&dwUser2, std::uint32_t&dwUser3, std::uint32_t&dwUser4);
        void dpSetSession(std::uint32_t dwUser1, std::uint32_t dwUser2, std::uint32_t dwUser3, std::uint32_t dwUser4);
        void dpSend(DPID sourceDplayId, DPID destDplayId, DWORD flags, LPVOID data, DWORD size);
        bool dpReceive(std::uint8_t* buffer, std::uint32_t& size, std::uint32_t& from, std::uint32_t& to);
        std::uint32_t dpCreatePlayer(const char* name);
        void dpDestroyPlayer(std::uint32_t dpid);
        void dpSetPlayerName(std::uint32_t id, const char* name);
        void dpEnumPlayers();

        void updateFoundSessionDescription(LPCDPSESSIONDESC2 lpFoundSD); //has to be public for the callback function
        static JDPlay* getInstance(); //makes the object available to the callback function

        DPSESSIONDESC2& enumSessions();

    private:
        void deInitialize();
        std::string getDPERR(HRESULT hr);
    };

}