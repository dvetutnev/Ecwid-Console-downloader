#pragma once

#include <uvw/tcp.hpp>

namespace uvw {

class TCPSocketWrapper : public Emitter<TCPSocketWrapper>
{
public:
    virtual void connect(const std::string& ip, unsigned short port) = 0;
    virtual void connect6(const std::string& ip, unsigned short port) = 0;
    virtual void read() = 0;
    virtual void stop() = 0;
    virtual void write(std::unique_ptr<char[]>, unsigned int) = 0;
    virtual void close() noexcept = 0;
    virtual ~TCPSocketWrapper() = default;
protected:
    struct ConstructorAccess { explicit ConstructorAccess(int) {} };
};

template< typename AIO >
class TCPSocketWrapperSimple final : public TCPSocketWrapper, public std::enable_shared_from_this< TCPSocketWrapperSimple<AIO> >
{
    using Loop = typename AIO::Loop;
    using TcpHandle = typename AIO::TcpHandle;

    using ErrorEvent = typename AIO::ErrorEvent;
    using ConnectEvent = typename AIO::ConnectEvent;
    using DataEvent = typename AIO::DataEvent;
    using EndEvent = typename AIO::EndEvent;
    using WriteEvent = typename AIO::WriteEvent;

public:
    TCPSocketWrapperSimple(ConstructorAccess, std::shared_ptr<Loop> loop_)
        : loop{ std::move(loop_) }
    {}
    static std::shared_ptr<TCPSocketWrapperSimple> create(std::shared_ptr<Loop>);

    virtual void connect(const std::string& ip, unsigned short port) override final { tcp_handle->template connect<uvw::IPv4>(ip, port); }
    virtual void connect6(const std::string& ip, unsigned short port) override final { tcp_handle->template connect<uvw::IPv6>(ip, port); }
    virtual void read() override final { tcp_handle->read(); }
    virtual void stop() override final { tcp_handle->stop(); }
    virtual void write(std::unique_ptr<char[]> data, unsigned int length) override final { tcp_handle->write(std::move(data), length); }
    virtual void close() noexcept override final { tcp_handle->close(); }

private:
    std::shared_ptr<Loop> loop;

    std::shared_ptr<TcpHandle> tcp_handle;

    template< typename Event >
    void on_event(Event&& event) { publish( std::forward<Event>(event) ); }
};

/* implemantation public TCPSocketWrapperSimple */

template< typename AIO >
std::shared_ptr< TCPSocketWrapperSimple<AIO> > TCPSocketWrapperSimple<AIO>::create(std::shared_ptr<Loop> loop)
{
    auto resource = std::make_shared< TCPSocketWrapperSimple<AIO> >( ConstructorAccess{42}, std::move(loop) );
    resource->tcp_handle = resource->loop->template resource<TcpHandle>();
    if (resource->tcp_handle)
    {
        resource->tcp_handle->template on<ErrorEvent>( [resource](auto& err, const auto&) { resource->on_event(err); } );
        resource->tcp_handle->template on<ConnectEvent>( [resource](auto& event, const auto&) { resource->on_event(event); } );
        resource->tcp_handle->template on<DataEvent>( [resource](auto& data, const auto&) { resource->on_event( std::move(data) ); } );
        resource->tcp_handle->template on<EndEvent>( [resource](auto& event, const auto&) { resource->on_event(event); } );
        resource->tcp_handle->template on<WriteEvent>( [resource](auto& event, const auto&) { resource->on_event(event); } );
        return resource;
    }
    return nullptr;
}

} // namespace uvw
