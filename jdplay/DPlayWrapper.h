#ifndef _DPLAY_WRAPPER_H
#define _DPLAY_WRAPPER_H

#include <dplay.h>
#include <dplobby.h>
#include <Windows.h>

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

#endif
