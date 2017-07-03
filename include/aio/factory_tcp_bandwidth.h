#pragma once

#include "aio/factory_tcp.h"
#include "aio/bandwidth.h"

namespace aio {

class FactoryTCPSocketBandwidth : public FactoryTCPSocket
{
public:
    FactoryTCPSocketBandwidth(std::shared_ptr<uvw::Loop> loop_, std::shared_ptr<bandwidth::Controller> controller_) noexcept
        : FactoryTCPSocket{ std::move(loop_) },
          controller{ std::move(controller_) }
    {}

    virtual std::shared_ptr<TCPSocket> tcp() override;
    virtual std::shared_ptr<TCPSocket> tcp_tls() override;

    FactoryTCPSocketBandwidth() = delete;
    FactoryTCPSocketBandwidth(const FactoryTCPSocketBandwidth&) = delete;
    FactoryTCPSocketBandwidth& operator= (const FactoryTCPSocketBandwidth&) = delete;
    FactoryTCPSocketBandwidth(FactoryTCPSocketBandwidth&&) = delete;
    FactoryTCPSocketBandwidth& operator= (FactoryTCPSocketBandwidth&&) = delete;

    virtual ~FactoryTCPSocketBandwidth() = default;

private:
    std::shared_ptr<bandwidth::Controller> controller;
};

} // namespace aio
