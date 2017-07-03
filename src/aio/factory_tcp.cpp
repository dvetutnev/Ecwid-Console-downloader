#include "aio/factory_tcp.h"
#include "aio/tcp_simple.h"
#include "aio_uvw.h"

using ::std::shared_ptr;

using ::aio::FactoryTCPSocket;
using ::aio::TCPSocket;
using ::aio::TCPSocketSimple;

shared_ptr<TCPSocket> FactoryTCPSocket::tcp()
{
    return loop->resource< TCPSocketSimple<AIO_UVW> >();
}

shared_ptr<TCPSocket> FactoryTCPSocket::tcp_tls()
{
    return nullptr;
}
