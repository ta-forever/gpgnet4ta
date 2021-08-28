#pragma once

#include <dplay.h>
#include <dplobby.h>
#include <windows.h>

namespace jdplay {

    class DPlayWrapper
    {
    public:
        DPlayWrapper();
        HRESULT directPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnk);
        HRESULT directPlayLobbyCreate(LPGUID, LPDIRECTPLAYLOBBY*, IUnknown*, LPVOID, DWORD);

    private:

        IID m_dpGuid;
        static HMODULE m_dll;
    };

}
