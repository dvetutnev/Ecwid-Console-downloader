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
    virtual void stop() noexcept override final { tcp_handle->stop(); }
    virtual void write(std::unique_ptr<char[]> data, unsigned int length) override final { tcp_handle->write(std::move(data), length); }
    virtual void close() noexcept override final;

private:
    std::shared_ptr<Loop> loop;
    bool closing = false;

    std::shared_ptr<TcpHandle> tcp_handle;

    bool is_closing() noexcept { return closing; }

    template < typename Event >
    void on_event(Event&, const TcpHandle&);
};

/* implemantation public TCPSocketWrapperSimple */

template< typename AIO >
std::shared_ptr< TCPSocketWrapperSimple<AIO> > TCPSocketWrapperSimple<AIO>::create(std::shared_ptr<Loop> loop)
{
    auto self = std::make_shared<TCPSocketWrapperSimple>( ConstructorAccess{42}, std::move(loop) );
    auto tcp_handle = self->loop->template resource<TcpHandle>();
    if ( !tcp_handle )
        return nullptr;

    using namespace std::placeholders;
    tcp_handle->template once<ErrorEvent>( std::bind(&TCPSocketWrapperSimple::on_event<ErrorEvent>, self, _1, _2) );
    tcp_handle->template once<ConnectEvent>( std::bind(&TCPSocketWrapperSimple::on_event<ConnectEvent>, self, _1, _2) );
    tcp_handle->template once<DataEvent>( std::bind(&TCPSocketWrapperSimple::on_event<DataEvent>, self, _1, _2) );
    tcp_handle->template once<EndEvent>( std::bind(&TCPSocketWrapperSimple::on_event<EndEvent>, self, _1, _2) );
    tcp_handle->template once<WriteEvent>( std::bind(&TCPSocketWrapperSimple::on_event<WriteEvent>, self, _1, _2) );

    self->tcp_handle = std::move(tcp_handle);
    return self;
}

template < typename AIO >
template < typename Event >
void TCPSocketWrapperSimple<AIO>::on_event(Event& event, const TcpHandle&)
{
    publish( std::move(event) );

    using namespace std::placeholders;
    if ( !is_closing() )
        tcp_handle->template once<Event>( std::bind(&TCPSocketWrapperSimple::on_event<Event>, this->template shared_from_this(), _1, _2) );
}

template < typename AIO >
void TCPSocketWrapperSimple<AIO>::close() noexcept
{
    tcp_handle->clear();
    tcp_handle->close();
    closing = true;
}

} // namespace uvw
