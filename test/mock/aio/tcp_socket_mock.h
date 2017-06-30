#pragma once

#include <gmock/gmock.h>
#include "aio/tcp.h"

struct TCPSocketMock : public uvw::TCPSocket
{
    MOCK_METHOD2( connect, void(const std::string&, unsigned short) );
    MOCK_METHOD2( connect6, void(const std::string&, unsigned short) );
    MOCK_METHOD0( read, void() );
    MOCK_METHOD0( stop, void() );
    virtual void write(std::unique_ptr<char[]> ptr, std::size_t len) { write_(ptr.get(), len); }
    MOCK_METHOD2( write_, void(const char[], std::size_t) );
    MOCK_METHOD0( shutdown, void() );
    virtual bool active() const noexcept { return  active_(); }
    MOCK_CONST_METHOD0( active_, bool() );
    virtual void close() noexcept { close_(); }
    MOCK_METHOD0( close_, void() );

    template< typename Event >
    void publish(Event&& event) { uvw::TCPSocket::publish( std::forward<Event>(event) ); }
};
