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
    using ConnectEvent = typename AIO::ConnectEvent;

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
    void on_connect();

    std::pair< std::unique_ptr<char[]>, unsigned int > make_request() const;
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
        std::string err_str = "Host <" + self->uri_parsed->host + "> can`t resolve. " + ErrorEvent2str(err);
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
    if (socket)
    {
        socket->clear();
        socket->close();
    }
    if (net_timer)
    {
        net_timer->clear();
        net_timer->close();
    }
}

template< typename AIO, typename Parser >
template< typename String >
std::enable_if_t< std::is_convertible<String, std::string>::value, void>
DownloaderSimple<AIO, Parser>::on_error(String&& str)
{
    on_error_without_tick( std::forward<String>(str) );
    on_tick->invoke( this->template shared_from_this() );
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_resolve(const AddrInfoEvent& event)
{
    socket = loop.template resource<TCPSocketSimple>();
    if (!socket)
    {
        on_error("Socket can`t create!");
        return;
    }

    net_timer = loop.template resource<Timer>();
    if (!net_timer)
    {
        on_error("Net_timer can`t create!");
        return;
    }

    const auto addr = AIO::addrinfo2IPAddress( event.data.get() );
    auto self = this->template shared_from_this();

    socket->template once<ErrorEvent>( [self, addr](const auto& err, const auto&) { self->on_error( "Host <" + addr.ip + "> can`t available. " + ErrorEvent2str(err) ); } );
    socket->template once<ConnectEvent>( [self](const auto&, const auto&) { self->on_connect(); } );
    if (addr.v6)
        socket->connect6(addr.ip, uri_parsed->port);
    else
        socket->connect(addr.ip, uri_parsed->port);

    net_timer->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error( "Net_timer run failed! " + ErrorEvent2str(err) ); } );
    net_timer->template once<TimerEvent>( [self, addr](const auto&, const auto&) { self->on_error("Timeout connect to host <" + addr.ip + ">"); } );
    net_timer->start( std::chrono::seconds{5}, std::chrono::seconds{0} );

    if ( m_status.state != StatusDownloader::State::Failed)
    {
        m_status.state_str = "Host Resolved. Connect to <" + addr.ip + ">";
        on_tick->invoke(self);
    }
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_connect()
{
    socket->clear();
    net_timer->template clear<TimerEvent>();
    net_timer->stop();

    auto self = this->template shared_from_this();

    socket->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error( "Request failed. " + ErrorEvent2str(err) ); } );
    auto request = make_request();
    socket->write( std::move(request.first), request.second );

    net_timer->template once<TimerEvent>( [self](const auto&, const auto&) { self->on_error("Timeout write request"); } );
    net_timer->start( std::chrono::seconds{5}, std::chrono::seconds{0} );

    m_status.state_str = "Connected, write request";
    on_tick->invoke(self);
}

template< typename AIO, typename Parser >
std::pair< std::unique_ptr<char[]>, unsigned int > DownloaderSimple<AIO, Parser>::make_request() const
{
    const std::string query = ""
            "GET " + uri_parsed->query + " HTTP/1.1\r\n"
            "Host: " + uri_parsed->host + "\r\n"
            "\r\n";
    auto raw_ptr = new char[ query.size() ];
    std::copy( std::begin(query), std::end(query), raw_ptr );
    return std::make_pair( std::unique_ptr<char[]>{raw_ptr}, query.size() );
}
