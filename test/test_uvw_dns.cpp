#include <gtest/gtest.h>

#include "aio_uvw.h"

using namespace std;

TEST(aio_uvw__addrinfo2IPAddress, nullptr_args)
{
    ASSERT_THROW( AIO_UVW::addrinfo2IPAddress(nullptr), std::invalid_argument );
}

TEST(aio_uvw__addrinfo2IPAddress, normal_ipv4)
{
    sockaddr_in addr_;
    uv_ip4_addr("127.0.0.1", 0, &addr_);
    addrinfo addrinfo_;
    addrinfo_.ai_family = AF_INET;
    addrinfo_.ai_addr = reinterpret_cast<sockaddr*>(&addr_);
    addrinfo_.ai_addrlen = sizeof (addr_);

    auto result = AIO_UVW::addrinfo2IPAddress(&addrinfo_);
    ASSERT_EQ(result.ip, "127.0.0.1");
    ASSERT_FALSE(result.v6);
}

TEST(aio_uvw__addrinfo2IPAddress, normal_ipv6)
{
    sockaddr_in6 addr_;
    uv_ip6_addr("::1", 0, &addr_);
    addrinfo addrinfo_;
    addrinfo_.ai_family = AF_INET6;
    addrinfo_.ai_addr = reinterpret_cast<sockaddr*>(&addr_);
    addrinfo_.ai_addrlen = sizeof (addr_);

    auto result = AIO_UVW::addrinfo2IPAddress(&addrinfo_);
    ASSERT_EQ(result.ip, "::1");
    ASSERT_TRUE(result.v6);
}

TEST(aio_uvw__GetAddrInfoReq, loopback)
{
    auto loop = ::uvw::Loop::getDefault();
    auto resolver = loop->resource<::uvw::GetAddrInfoReq>();
    const string host = "127.0.0.1";

    resolver->on<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); }
                );
    bool resolved = false;
    resolver->on<::uvw::AddrInfoEvent>( [&resolved, &host](const auto& event, auto&)
    {
        resolved = true;
        auto address = AIO_UVW::addrinfo2IPAddress( event.data.get() );
        ASSERT_EQ(address.ip, host);
        cout << "event.data->ai_flags => " << event.data->ai_flags << endl;
        cout << "event.data->ai_family => " << ( (event.data->ai_family == AF_INET) ? "AF_INET" : "AF_INET6" ) << endl;
        cout << "event.data->ai_socktype => " << event.data->ai_socktype << endl;
        cout << "event.data->ai_protocol => " << event.data->ai_protocol << endl;
    }
                );
    resolver->nodeAddrInfo(host);
    loop->run();
    ASSERT_TRUE(resolved);
}

TEST(aio_uvw__GetAddrInfoReq, bad_uri)
{
    auto loop = ::uvw::Loop::getDefault();
    auto resolver = loop->resource<::uvw::GetAddrInfoReq>();

    bool failed = false;
    resolver->on<::uvw::ErrorEvent>( [&failed](const auto& err, auto&)
    {
        failed = true;
        cout << "ErrorEvent: what => " << err.what() << " code => " << err.code() << boolalpha << " bool => " << static_cast<bool>(err) << endl;
    }
                );
    resolver->on<::uvw::AddrInfoEvent>( [](const auto&, auto&) { FAIL(); }
                );
    resolver->nodeAddrInfo("bad_uri");
    loop->run();
    ASSERT_TRUE(failed);
}
