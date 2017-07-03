#pragma once

#include "aio/tcp.h"

namespace uvw {
class Loop;
}

namespace aio {

class FactoryTCPSocket
{
public:
    FactoryTCPSocket(std::shared_ptr<uvw::Loop> loop_) noexcept
        : loop{ std::move(loop_) }
    {}

    virtual std::shared_ptr<TCPSocket> tcp();
    virtual std::shared_ptr<TCPSocket> tcp_tls();

    FactoryTCPSocket() = delete;
    FactoryTCPSocket(const FactoryTCPSocket&) = delete;
    FactoryTCPSocket& operator= (const FactoryTCPSocket&) = delete;
    FactoryTCPSocket(FactoryTCPSocket&&) = delete;
    FactoryTCPSocket& operator= (FactoryTCPSocket&&) = delete;

    virtual ~FactoryTCPSocket() = default;

private:
    std::shared_ptr<uvw::Loop> loop;
};

} // namespace aio
