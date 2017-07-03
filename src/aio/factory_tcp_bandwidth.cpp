#include "aio/factory_tcp_bandwidth.h"
#include "aio/tcp_bandwidth.h"

using ::std::shared_ptr;

using ::aio::FactoryTCPSocketBandwidth;
using ::aio::TCPSocket;
using ::aio::TCPSocketBandwidth;

shared_ptr<TCPSocket> FactoryTCPSocketBandwidth::tcp()
{
    auto socket = FactoryTCPSocket::tcp();
    return (socket) ? TCPSocketBandwidth::create(nullptr, controller, socket) : nullptr;
}

shared_ptr<TCPSocket> FactoryTCPSocketBandwidth::tcp_tls()
{
    auto socket = FactoryTCPSocket::tcp_tls();
    return (socket) ? TCPSocketBandwidth::create(nullptr, controller, socket) : nullptr;
}
