#pragma once

#include <chrono>

#include "downloader.h"
#include "on_tick.h"

template< typename AIO, typename Parser >
class DownloaderSimple : public Downloader, public std::enable_shared_from_this< DownloaderSimple<AIO, Parser> >
{
    using Loop = typename AIO::Loop;
    using ErrorEvent = typename AIO::ErrorEvent;

    using UriParseResult = typename Parser::UriParseResult;

    using AddrInfoEvent = typename AIO::AddrInfoEvent;
    using GetAddrInfoReq = typename AIO::GetAddrInfoReq;

    using TCPSocket = typename AIO::TCPSocketWrapper;
    using TCPSocketSimple = typename AIO::TCPSocketWrapperSimple;

    using Timer = typename AIO::TimerHandle;
    using TimerEvent = typename AIO::TimerEvent;

public:
    DownloaderSimple(Loop& loop_, std::shared_ptr<OnTick> on_tick_)
        : loop{loop_},
          on_tick{on_tick_}
    {}

    virtual bool run(const Task&) override final;
    virtual void stop() override final;
    virtual const StatusDownloader& status() const override final { return m_status; }

private:
    Loop& loop;
    std::shared_ptr<OnTick> on_tick;

    StatusDownloader m_status;
    std::unique_ptr<UriParseResult> uri_parsed;
    std::shared_ptr<TCPSocket> socket;
    std::shared_ptr<Timer> net_timer;

    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    on_error_without_tick(String&&);
    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    on_error(String&&);

    void on_resolve(const AddrInfoEvent&);
};


/* -- implementation, because template( -- */
template< typename ErrorEvent >
inline std::string ErrorEvent2str(const ErrorEvent& err)
{
    return "Code => " + std::to_string(err.code()) + " Reason => " + err.what();
}

template< typename AIO, typename Parser >
bool DownloaderSimple<AIO, Parser>::run(const Task& task)
{
    m_status.state = StatusDownloader::State::Init;
    uri_parsed = Parser::uri_parse(task.uri);
    if (!uri_parsed)
        return false;

    auto resolver = loop.template resource<GetAddrInfoReq>();
    auto self = this->template shared_from_this();

    resolver->template once<ErrorEvent>( [self](const auto& err, const auto&)
    {
        std::string err_str = "Can`t resolve " + self->uri_parsed->host + " " + ErrorEvent2str(err);
        if ( self->m_status.state == StatusDownloader::State::OnTheGo )
            self->on_error( std::move(err_str) );
        else
            self->on_error_without_tick( std::move(err_str) );
    } );
    resolver->template once<AddrInfoEvent>( [self](const auto& event, const auto&) { self->on_resolve(event); } );

    resolver->getNodeAddrInfo(uri_parsed->host);

    if ( m_status.state == StatusDownloader::State::Init )
    {
        m_status.state = StatusDownloader::State::OnTheGo;
        return true;
    }
    return false;
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::stop()
{

}

template< typename AIO, typename Parser >
template< typename String >
std::enable_if_t< std::is_convertible<String, std::string>::value, void>
DownloaderSimple<AIO, Parser>::on_error_without_tick(String&& str)
{
    m_status.state = StatusDownloader::State::Failed;
    m_status.state_str = std::forward<String>(str);
}

template< typename AIO, typename Parser >
template< typename String >
std::enable_if_t< std::is_convertible<String, std::string>::value, void>
DownloaderSimple<AIO, Parser>::on_error(String&& str)
{
    on_error_without_tick( std::forward<String>(str) );
    if (socket)
        socket->close();
    if (net_timer)
        net_timer->close();
    on_tick->invoke( this->template shared_from_this() );
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_resolve(const AddrInfoEvent& event)
{
    const auto addr = AIO::addrinfo2IPAddress( event.data.get() );
    m_status.state_str = "Host Resolved. Connect to " + addr.ip;
    auto self = this->template shared_from_this();
    on_tick->invoke(self);

    socket = loop.template resource<TCPSocketSimple>();
    if (!socket)
    {
        on_error("Can`t create socket");
        return;
    }

    net_timer = loop.template resource<Timer>();
    if (!net_timer)
    {
        on_error("Can`t create net_timer!");
        return;
    }


    socket->template once<ErrorEvent>( [self, addr](const auto& err, const auto&) { self->on_error( "Can`t connect to " + addr.ip + " " + ErrorEvent2str(err) ); } );

    socket->connect(addr.ip, uri_parsed->port);

    net_timer->template once<TimerEvent>( [self, addr](const auto&, const auto&) { self->on_error("Timeout connect to " + addr.ip); } );
    net_timer->start( std::chrono::seconds{5}, std::chrono::seconds{0} );
}
