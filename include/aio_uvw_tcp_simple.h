#pragma once

#include "aio_uvw_tcp.h"

#include <cassert>
#include <limits>

namespace uvw {

template< typename AIO >
class TCPSocketSimple final : public TCPSocket, public std::enable_shared_from_this< TCPSocketSimple<AIO> >
{
    using Loop = typename AIO::Loop;
    using TcpHandle = typename AIO::TcpHandle;

    using ErrorEvent = typename AIO::ErrorEvent;
    using ConnectEvent = typename AIO::ConnectEvent;
    using DataEvent = typename AIO::DataEvent;
    using EndEvent = typename AIO::EndEvent;
    using WriteEvent = typename AIO::WriteEvent;
    using ShutdownEvent = typename AIO::ShutdownEvent;

public:
    TCPSocketSimple(ConstructorAccess) {}
    static std::shared_ptr<TCPSocketSimple> create(std::shared_ptr<Loop>);

    virtual void connect(const std::string& ip, unsigned short port) override { tcp_handle->template connect<uvw::IPv4>(ip, port); }
    virtual void connect6(const std::string& ip, unsigned short port) override { tcp_handle->template connect<uvw::IPv6>(ip, port); }
    virtual void read() override { tcp_handle->read(); }
    virtual void stop() noexcept override { tcp_handle->stop(); }
    virtual void write(std::unique_ptr<char[]> data, std::size_t length) override
    {
        assert(length <= std::numeric_limits<unsigned int>::max());
        tcp_handle->write(std::move(data), length);
    }
    virtual void shutdown() override { tcp_handle->shutdown(); }
    virtual bool active() const noexcept override { return tcp_handle->active(); }
    virtual void close() noexcept override
    {
        if (!closed)
        {
            closed = true;
            tcp_handle->clear();
            tcp_handle->template once<CloseEvent>( bind<CloseEvent>(this->template shared_from_this()) );
            tcp_handle->close();
        }
    }

private:
    std::shared_ptr<Loop> loop;
    bool closed = false;

    std::shared_ptr<TcpHandle> tcp_handle;

    template < typename Event >
    void on_event(Event& event, const TcpHandle&)
    {
        publish( std::move(event) );
        if (!closed)
            tcp_handle->template once<Event>( bind<Event>(this->template shared_from_this()) );
    }

    template < typename Event >
    static std::function<void(Event&, const TcpHandle&)> bind(std::shared_ptr<TCPSocketSimple> self)
    {
        using namespace std::placeholders;
        return std::bind(&TCPSocketSimple::on_event<Event>, std::move(self), _1, _2);
    }
};

template< typename AIO >
std::shared_ptr< TCPSocketSimple<AIO> > TCPSocketSimple<AIO>::create(std::shared_ptr<Loop> loop)
{
    auto self = std::make_shared<TCPSocketSimple>( ConstructorAccess{42} );
    auto tcp_handle = loop->template resource<TcpHandle>();
    if ( !tcp_handle )
        return nullptr;

    tcp_handle->template once<ErrorEvent>( bind<ErrorEvent>(self) );
    tcp_handle->template once<ConnectEvent>( bind<ConnectEvent>(self) );
    tcp_handle->template once<DataEvent>( bind<DataEvent>(self) );
    tcp_handle->template once<EndEvent>( bind<EndEvent>(self) );
    tcp_handle->template once<WriteEvent>( bind<WriteEvent>(self) );
    tcp_handle->template once<ShutdownEvent>( bind<ShutdownEvent>(self) );

    self->tcp_handle = std::move(tcp_handle);
    return self;
}

} // namespace uvw
