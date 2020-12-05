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

#include <dplay.h>
#include <dplobby.h>
#include <string>

BOOL FAR PASCAL EnumSessionsCallback(LPCDPSESSIONDESC2 lpThisSD, LPDWORD lpdwTimeOut, DWORD dwFlags, LPVOID lpContext);

class JDPlay {
private:
    static JDPlay* instance;		// needed for callback function to access a method

    bool debug;
    int curRetry;
    int searchValidationCount;
    bool foundLobby;
    bool isInitialized;
    bool lpDPIsOpen;
    std::string enumCallbackSessionName;
    std::string enumCallbackSessionPassword;

    LPDIRECTPLAY3A lpDP;		// directplay interface pointer
    LPDIRECTPLAYLOBBY3A	lpDPLobby;	// lobby interface pointer

    DPNAME dpName;			// player description
    DPSESSIONDESC2 dpSessionDesc;       // session description
    DPLCONNECTION dpConn;		// connection description

    DPID dPid;			        // player ID (currently unused)
    DWORD appID;			// game process ID
    DWORD sessionFlags;                 // either Host or Join Session
    DWORD playerFlags;                  // either Host or not
    HANDLE processHandle;               // of game after launching

public:
    JDPlay(const char* playerName, int searchValidationCount, bool debug);
    ~JDPlay();

    void updatePlayerName(const char* playerName);
    bool initialize(const char* gameGUID, char* hostIP, bool isHost, int maxPlayers);
    bool searchOnce();
    bool launch(bool startGame);
    bool pollStillActive();
    void pollSessionStatus(LPDIRECTPLAY3A dplay = NULL);
    void printSessionDesc();
    bool isHost();
    void releaseDirectPlay();
    void releaseLobby();
    std::string getAdvertisedSessionName();
    std::string getAdvertisedPlayerName();
    DWORD_PTR getUserData1() { return dpSessionDesc.dwUser1; }
    DWORD_PTR getUserData2() { return dpSessionDesc.dwUser2; }
    DWORD_PTR getUserData3() { return dpSessionDesc.dwUser3; }
    DWORD_PTR getUserData4() { return dpSessionDesc.dwUser4; }

    void updateFoundSessionDescription(LPCDPSESSIONDESC2 lpFoundSD); //has to be public for the callback function
    static JDPlay* getInstance(); //makes the object available to the callback function

private:
    void deInitialize();
    const char* getDPERR(HRESULT hr);
};
