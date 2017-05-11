#pragma once

#include <gmock/gmock.h>
#include <uvw/tcp.hpp>

struct TcpHandleMock;

namespace TcpHandleMock_internal {

template< typename T >
void connect(TcpHandleMock&, const std::string&, unsigned short) {}
template<>
void connect<uvw::IPv4>(TcpHandleMock&, const std::string&, unsigned short);
template<>
void connect<uvw::IPv6>(TcpHandleMock&, const std::string&, unsigned short);

}

struct TcpHandleMock : public uvw::Emitter<TcpHandleMock>
{
    template< typename T >
    std::enable_if_t< std::is_same<T, uvw::IPv4>::value || std::is_same<T, uvw::IPv6>::value, void>
    connect(const std::string& ip, unsigned short port) { TcpHandleMock_internal::connect<T>(*this, ip, port); }

    MOCK_METHOD2( connect, void(const std::string&, unsigned short) );
    MOCK_METHOD2( connect6, void(const std::string&, unsigned short) );
    MOCK_METHOD0( read, void() );
    MOCK_METHOD0( stop, void() );
    void write(std::unique_ptr<char[]>ptr, unsigned int len) { write_(ptr.get(), len); }
    MOCK_METHOD2( write_, void(char[], unsigned int) );
    MOCK_METHOD0( shutdown, void() );
    MOCK_METHOD0( close, void() );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<TcpHandleMock>::publish( std::forward<Event>(event) ); }
};

namespace TcpHandleMock_internal {

template<>
void connect<uvw::IPv4>(TcpHandleMock& self, const std::string& ip, unsigned short port) { self.connect(ip, port); }
template<>
void connect<uvw::IPv6>(TcpHandleMock& self, const std::string& ip, unsigned short port) { self.connect6(ip, port); }

}
