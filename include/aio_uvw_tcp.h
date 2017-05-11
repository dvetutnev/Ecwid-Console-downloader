#pragma once

#include <cassert>
#include <limits>

#include <uvw/tcp.hpp>

namespace uvw {

class TCPSocketWrapper : public Emitter<TCPSocketWrapper>
{
public:
    virtual void connect(const std::string& ip, unsigned short port) = 0;
    virtual void connect6(const std::string& ip, unsigned short port) = 0;
    virtual void read() = 0;
    virtual void stop() = 0;
    virtual void write(std::unique_ptr<char[]>, std::size_t) = 0;
    virtual void shutdown() = 0;
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
    using ShutdownEvent = typename AIO::ShutdownEvent;

public:
    TCPSocketWrapperSimple(ConstructorAccess, std::shared_ptr<Loop> loop_)
        : loop{ std::move(loop_) }
    {}
    static std::shared_ptr<TCPSocketWrapperSimple> create(std::shared_ptr<Loop>);

    virtual void connect(const std::string& ip, unsigned short port) override final { tcp_handle->template connect<uvw::IPv4>(ip, port); }
    virtual void connect6(const std::string& ip, unsigned short port) override final { tcp_handle->template connect<uvw::IPv6>(ip, port); }
    virtual void read() override final { tcp_handle->read(); }
    virtual void stop() noexcept override final { tcp_handle->stop(); }
    virtual void write(std::unique_ptr<char[]> data, std::size_t length) override final
    {
        assert(length <= std::numeric_limits<unsigned int>::max());
        tcp_handle->write(std::move(data), length);
    }
    virtual void shutdown() override final { tcp_handle->shutdown(); }
    virtual void close() noexcept override final
    {
        closing = true;
        tcp_handle->clear();
        tcp_handle->template once<CloseEvent>( bind<CloseEvent>(this->template shared_from_this()) );
        tcp_handle->close();
    }

private:
    std::shared_ptr<Loop> loop;
    bool closing = false;

    std::shared_ptr<TcpHandle> tcp_handle;

    bool is_closing() const noexcept { return closing; }

    template < typename Event >
    void on_event(Event& event, const TcpHandle&)
    {
        publish( std::move(event) );
        if ( !is_closing() )
            tcp_handle->template once<Event>( bind<Event>(this->template shared_from_this()) );
    }

    template < typename Event >
    static std::function<void(Event&, const TcpHandle&)> bind(std::shared_ptr<TCPSocketWrapperSimple> self)
    {
        using namespace std::placeholders;
        return std::bind(&TCPSocketWrapperSimple::on_event<Event>, std::move(self), _1, _2);
    }
};

template< typename AIO >
std::shared_ptr< TCPSocketWrapperSimple<AIO> > TCPSocketWrapperSimple<AIO>::create(std::shared_ptr<Loop> loop)
{
    auto self = std::make_shared<TCPSocketWrapperSimple>( ConstructorAccess{42}, std::move(loop) );
    auto tcp_handle = self->loop->template resource<TcpHandle>();
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
