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
    using WriteEvent = typename AIO::WriteEvent;
    using DataEvent = typename AIO::DataEvent;
    using EndEvent = typename AIO::EndEvent;

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
    std::unique_ptr<Parser> http_parser;

    void close_handles();
    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    on_error_without_tick(String&&);
    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    on_error(String&&);
    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    update_status(String&&);

    void on_resolve(const AddrInfoEvent&);
    void on_connect();
    void on_write_http_request();
    void on_read(std::unique_ptr<char[]>, std::size_t);

    std::pair< std::unique_ptr<char[]>, std::size_t > make_request() const;
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
    using State = StatusDownloader::State;
    m_status.state = State::Init;
    uri_parsed = Parser::uri_parse(task.uri);
    if (!uri_parsed)
        return false;

    auto resolver = loop.template resource<GetAddrInfoReq>();
    auto self = this->template shared_from_this();

    resolver->template once<ErrorEvent>( [self](const auto& err, const auto&)
    {
        std::string err_str = "Host <" + self->uri_parsed->host + "> can`t resolve. " + ErrorEvent2str(err);
        if ( self->m_status.state == State::OnTheGo )
            self->on_error( std::move(err_str) );
        else
            self->on_error_without_tick( std::move(err_str) );
    } );
    resolver->template once<AddrInfoEvent>( [self](const auto& event, const auto&) { self->on_resolve(event); } );

    resolver->getNodeAddrInfo(uri_parsed->host);

    if ( m_status.state == State::Init )
    {
        m_status.state = State::OnTheGo;
        return true;
    }
    return false;
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

    update_status("Host Resolved. Connect to <" + addr.ip + ">");
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_connect()
{
    socket->clear();
    net_timer->template clear<TimerEvent>();
    net_timer->stop();

    auto self = this->template shared_from_this();

    socket->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error( "Request failed. " + ErrorEvent2str(err) ); } );
    socket->template once<WriteEvent>( [self](const auto&, const auto&) { self->on_write_http_request(); } );
    auto request = make_request();
    socket->write( std::move(request.first), request.second );

    net_timer->template once<TimerEvent>( [self](const auto&, const auto&) { self->on_error("Timeout write request"); } );
    net_timer->start( std::chrono::seconds{5}, std::chrono::seconds{0} );

    update_status("Connected, write request.");
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_write_http_request()
{
    socket->clear();
    net_timer->template clear<TimerEvent>();
    net_timer->stop();

    auto self = this->template shared_from_this();

    socket->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error("Response read failed. " + ErrorEvent2str(err) ); } );
    socket->template once<EndEvent>( [self](const auto&, const auto&) { self->on_error("Connection it`s unexpecdly closed."); } );
    socket->template once<DataEvent>( [self](auto& event, const auto&)
    {
        self->socket->template clear<EndEvent>();
        self->socket->template once<EndEvent>( [self](const auto&, const auto&) { self->on_read(nullptr, 0); } );
        self->http_parser = Parser::create(nullptr);
        self->on_read( std::move(event.data), event.length );
    } );
    socket->read();

    net_timer->template once<TimerEvent>( [self](const auto&, const auto&) { self->on_error("Timeout read response"); } );
    net_timer->start( std::chrono::seconds{5}, std::chrono::seconds{0} );

    update_status("Write request done. Wait response.");
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_read(std::unique_ptr<char[]> data, std::size_t length)
{
    using Result = typename Parser::ResponseParseResult::State;

    auto result = http_parser->response_parse(std::move(data), length);
    auto self = this->template shared_from_this();

    if (result.state == Result::InProgress)
    {
        socket->template once<DataEvent>( [self](auto& event, const auto&) { self->on_read(std::move(event.data), event.length); } );
        net_timer->again();
    } else if(result.state == Result::Redirect)
    {
        m_status.state = StatusDownloader::State::Redirect;
        m_status.redirect_uri = std::move(result.redirect_uri);
        m_status.state_str = "Redirect to <" + m_status.redirect_uri + ">";
        close_handles();
        on_tick->invoke(self);
    } else if (result.state == Result::Done)
    {
        m_status.state = StatusDownloader::State::Done;
        close_handles();
        on_tick->invoke(self);
    } else if (result.state == Result::Error)
    {
        on_error("Response parse failed. " + std::move(result.err_str) );
    }

    update_status("Data received.");
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::stop()
{

}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::close_handles()
{
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
DownloaderSimple<AIO, Parser>::on_error_without_tick(String&& str)
{
    m_status.state = StatusDownloader::State::Failed;
    m_status.state_str = std::forward<String>(str);
    close_handles();
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
template< typename String >
std::enable_if_t< std::is_convertible<String, std::string>::value, void>
DownloaderSimple<AIO, Parser>::update_status(String&& str)
{
    if ( m_status.state == StatusDownloader::State::OnTheGo)
    {
        m_status.state_str = std::forward<String>(str);
        on_tick->invoke( this->template shared_from_this() );
    }
}

template< typename AIO, typename Parser >
std::pair<std::unique_ptr<char[]>, std::size_t> DownloaderSimple<AIO, Parser>::make_request() const
{
    const std::string query = ""
            "GET " + uri_parsed->query + " HTTP/1.1\r\n"
            "Host: " + uri_parsed->host + "\r\n"
            "\r\n";
    auto raw_ptr = new char[ query.size() ];
    std::copy( std::begin(query), std::end(query), raw_ptr );
    return std::make_pair( std::unique_ptr<char[]>{raw_ptr}, query.size() );
}
