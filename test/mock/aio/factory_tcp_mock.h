#pragma once

#include <gmock/gmock.h>
#include "aio/factory_tcp.h"

namespace aio {

struct FactoryTCPSocketMock : public FactoryTCPSocket
{
    FactoryTCPSocketMock()
        : FactoryTCPSocket{nullptr}
    {}

    MOCK_METHOD0( tcp, std::shared_ptr<TCPSocket>() );
    MOCK_METHOD0( tcp_tls, std::shared_ptr<TCPSocket>() );
};

} // namespace aio
