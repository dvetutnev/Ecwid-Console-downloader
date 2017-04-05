#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "aio_uvw.h"

TEST(aio_uvw__addrinfo2IPAddress, nullptr_args)
{
    ASSERT_THROW( aio_uvw::addrinfo2IPAddres(nullptr), std::invalid_argument );
}

TEST(aio_uvw__addrinfo2IPAddress, normal_ipv4)
{
    sockaddr_in addr_;
    uv_ip4_addr("127.0.0.1", 0, &addr_);
    addrinfo addrinfo_;
    addrinfo_.ai_family = AF_INET;
    addrinfo_.ai_addr = reinterpret_cast<sockaddr*>(&addr_);
    addrinfo_.ai_addrlen = sizeof (addr_);

    auto result = aio_uvw::addrinfo2IPAddres(&addrinfo_);
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

    auto result = aio_uvw::addrinfo2IPAddres(&addrinfo_);
    ASSERT_EQ(result.ip, "::1");
    ASSERT_TRUE(result.v6);
}
