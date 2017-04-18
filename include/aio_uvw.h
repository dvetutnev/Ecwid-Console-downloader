#pragma once

#include <uvw/loop.hpp>
#include <uvw/dns.hpp>
#include <uvw/timer.hpp>

#include "aio_uvw_tcp.h"

struct AIO_UVW
{
    using Loop = uvw::Loop;
    using ErrorEvent = uvw::ErrorEvent;

    struct IPAddress
    {
        IPAddress()
            : ip{},
              v6{false}
        {}

        template< typename String,
                  typename = std::enable_if_t< std::is_convertible<String, std::string>::value, String> >
        IPAddress(String&& ip_, bool v6_)
            : ip{ std::forward<String>(ip_) },
              v6{v6_}
        {}

        std::string ip;
        bool v6;
    };
    static const IPAddress addrinfo2IPAddress(const addrinfo*);

    using AddrInfoEvent = uvw::AddrInfoEvent;
    using GetAddrInfoReq = uvw::GetAddrInfoReq;

    using ConnectEvent = uvw::ConnectEvent;
    using DataEvent = uvw::DataEvent;
    using EndEvent = uvw::EndEvent;
    using WriteEvent = uvw::WriteEvent;
    using TCPSocketWrapper = uvw::TCPSocketWrapper;
    using TCPSocketWrapperSimple = uvw::TCPSocketWrapperSimple<AIO_UVW>;

    using TimerHandle = uvw::TimerHandle;
    using TimerEvent = uvw::TimerEvent;
};

const AIO_UVW::IPAddress AIO_UVW::addrinfo2IPAddress(const addrinfo* addr)
{
    if (addr == nullptr)
        throw std::invalid_argument{"addrinfo must not be NULL!"};

    char buf[128];

    if (addr->ai_family == AF_INET)
    {
        if ( uv_ip4_name( reinterpret_cast<sockaddr_in*>(addr->ai_addr), buf, sizeof (buf) ) == 0 )
            return IPAddress{buf, false};
    } else if (addr->ai_family == AF_INET6)
    {
        if ( uv_ip6_name( reinterpret_cast<sockaddr_in6*>(addr->ai_addr), buf, sizeof (buf) ) == 0 )
            return IPAddress{buf, true};
    }

    return IPAddress{};
}
