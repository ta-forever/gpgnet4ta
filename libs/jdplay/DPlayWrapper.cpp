#include "DPlayWrapper.h"
#include <string>
#include <iostream>

using namespace jdplay;

typedef HRESULT(WINAPI* TDirectPlayCreate) (LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnk);
//typedef HRESULT(WINAPI* TDirectPlayEnumerate) (LPDPENUMDPCALLBACK, LPVOID);
typedef HRESULT(WINAPI* TDirectPlayLobbyCreate) (LPGUID, LPDIRECTPLAYLOBBY*, IUnknown*, LPVOID, DWORD);

static TDirectPlayCreate pDirectPlayCreate = 0;
//static TDirectPlayEnumerate pDirectPlayEnumerate = 0;
static TDirectPlayLobbyCreate pDirectPlayLobbyCreate = 0;

HMODULE DPlayWrapper::m_dll = 0;

DPlayWrapper::DPlayWrapper()
{
    if (m_dll == 0)
    {
        char windir[MAX_PATH];
        GetWindowsDirectory(windir, MAX_PATH);

        std::string dllPath = std::string() + windir + "\\system32\\dplayx.dll";
        m_dll = LoadLibrary(dllPath.c_str());
        if (m_dll == 0)
        {
            throw std::runtime_error("[DPlayWrapper::DPlayWrapper] Unable to load " + dllPath);
        }

        pDirectPlayCreate = (TDirectPlayCreate)GetProcAddress(m_dll, "DirectPlayCreate");
        //pDirectPlayEnumerate = (TDirectPlayEnumerate)GetProcAddress(m_dll, "DirectPlayEnumerate");
        pDirectPlayLobbyCreate = (TDirectPlayLobbyCreate)GetProcAddress(m_dll, "DirectPlayLobbyCreateW");

        if (!pDirectPlayCreate || /*!pDirectPlayEnumerate || */!pDirectPlayLobbyCreate)
        {
            FreeLibrary(m_dll);
            m_dll = 0;
            throw std::runtime_error("DPlayWrapper::DPlayWrapper] DLL load failure " + dllPath);
        }

        std::cout << "dplayx.dll loaded ok" << std::endl;
    }
}

HRESULT DPlayWrapper::directPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnk)
{
    return pDirectPlayCreate(lpGUID, lplpDP, pUnk);
}

HRESULT DPlayWrapper::directPlayLobbyCreate(LPGUID e, LPDIRECTPLAYLOBBY* d, IUnknown* c, LPVOID b, DWORD a)
{
    return pDirectPlayLobbyCreate(e, d, c, b, a);
}
