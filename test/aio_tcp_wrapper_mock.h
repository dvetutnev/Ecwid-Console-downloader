#pragma once

#include <gmock/gmock.h>
#include "aio_uvw_tcp.h"

struct TCPSocketWrapperMock : public uvw::TCPSocketWrapper
{
    MOCK_METHOD2(connect, void(const std::string&, unsigned short));
    MOCK_METHOD2(connect6, void(const std::string&, unsigned short));
    MOCK_METHOD0(read, void());
    MOCK_METHOD0(stop, void());
    virtual void write(std::unique_ptr<char[]> ptr, unsigned int len) { write_(ptr.get(), len); }
    MOCK_METHOD2(write_, void(const char[], unsigned int));
    virtual void close() noexcept { close_(); }
    MOCK_METHOD0(close_, void());

    template< typename Event >
    void publish(Event&& event) { uvw::TCPSocketWrapper::publish( std::forward<Event>(event) ); }
};
