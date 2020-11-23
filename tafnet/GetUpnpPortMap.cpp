#include "GetUpnpPortMap.h"

#define MINIUPNP_STATICLIB
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include "miniupnpc/portlistingparse.h"


#include <winsock.h>
#include <string>
#include <sstream>

namespace tafnet
{

    std::uint16_t GetUpnpPortMap(std::uint16_t _extPort, const char *TCP_or_UDP)
    {
        std::string extPort;
        {
            std::ostringstream ss;
            ss << _extPort;
            extPort = ss.str();
        }

        WSADATA wsaData;
        char lan_address[64];
        struct UPNPUrls upnp_urls;
        struct IGDdatas upnp_data;

        int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (nResult != NO_ERROR)
        {
            throw std::runtime_error("WSAStartup() failed");
        }

        int error = 0;
        struct UPNPDev* upnp_dev = upnpDiscover(
            2000, // time to wait (milliseconds)
            nullptr, // multicast interface (or null defaults to 239.255.255.250)
            nullptr, // path to minissdpd socket (or null defaults to /var/run/minissdpd.sock)
            0, // source port to use (or zero defaults to port 1900)
            0, // 0==IPv4, 1==IPv6
            2,
            &error); // error condition
        if (error != 0)
        {
            WSACleanup();
            throw std::runtime_error("Unable to discover UPNP network devices");
        }

        int status = UPNP_GetValidIGD(upnp_dev, &upnp_urls, &upnp_data, lan_address, sizeof(lan_address));
        // look up possible "status" values, the number "1" indicates a valid IGD was found
        if (status != 1)
        {
            WSACleanup();
            throw std::runtime_error("No Internet Gateway Device (IGD) found");
        }

        // get the external (WAN) IP address
        char wan_address[64];
        error = UPNP_GetExternalIPAddress(upnp_urls.controlURL, upnp_data.first.servicetype, wan_address);
        if (error != 0)
        {
            throw std::runtime_error("Unable to determine WAN IP address");
        }

        char intClient[16];
        char intPort[6];
        error = UPNP_GetSpecificPortMappingEntry(upnp_urls.controlURL, upnp_data.first.servicetype, extPort.c_str(), TCP_or_UDP, wan_address,
            intClient, intPort, NULL, NULL, NULL);
        if (error != 0)
        {
            WSACleanup();
            throw std::runtime_error("Unable to get list of upnp port mappings");
        }

        WSACleanup();
        return std::atoi(intPort);
    }

    void GetUpnpPortMaps(const char *internalClient, std::map<std::uint16_t, std::uint16_t> &portMapsTcp, std::map<std::uint16_t, std::uint16_t> &portMapsUdp)
    {
        WSADATA wsaData;
        char lan_address[64];
        struct UPNPUrls upnp_urls;
        struct IGDdatas upnp_data;

        int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (nResult != NO_ERROR)
        {
            throw std::runtime_error("WSAStartup() failed");
        }

        int error = 0;
        struct UPNPDev* upnp_dev = upnpDiscover(
            2000, // time to wait (milliseconds)
            nullptr, // multicast interface (or null defaults to 239.255.255.250)
            nullptr, // path to minissdpd socket (or null defaults to /var/run/minissdpd.sock)
            0, // source port to use (or zero defaults to port 1900)
            0, // 0==IPv4, 1==IPv6
            2,
            &error); // error condition
        if (error != 0)
        {
            WSACleanup();
            throw std::runtime_error("Unable to discover UPNP network devices");
        }

        int status = UPNP_GetValidIGD(upnp_dev, &upnp_urls, &upnp_data, lan_address, sizeof(lan_address));
        // look up possible "status" values, the number "1" indicates a valid IGD was found
        if (status != 1)
        {
            WSACleanup();
            throw std::runtime_error("No Internet Gateway Device (IGD) found");
        }

        PortMappingParserData data;
        //error = UPNP_GetListOfPortMappings(upnp_urls.controlURL, upnp_data.first.servicetype, "2300", "65535", "TCP", NULL, &data);
        error = UPNP_GetListOfPortMappings(upnp_urls.controlURL, upnp_data.first.servicetype, "2300", "65535", "TCP", NULL, &data);
        if (error != 0)
        {
            WSACleanup();
            throw std::runtime_error("Unable to get list of upnp port mappings");
        }

        for (PortMapping *pm = data.l_head; pm != NULL; pm = pm->l_next)
        {
            if (internalClient == NULL || std::strcmp(pm->internalClient, internalClient) == 0)
            {
                if (std::strcmp(pm->protocol, "TCP") == 0)
                {
                    portMapsTcp[pm->externalPort] = pm->internalPort;
                }
                else if (std::strcmp(pm->protocol, "UDP") == 0)
                {
                    portMapsUdp[pm->externalPort] = pm->internalPort;
                }
            }
        }

        FreePortListing(&data);
        WSACleanup();
    }

}