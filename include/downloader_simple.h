#pragma once

#include "downloader.h"
#include "on_tick.h"
#include "data_chunk.h"

#include <chrono>
#include <queue>
#include <fcntl.h>
#include <cassert>
#include <limits>

template< typename AIO, typename Parser >
class DownloaderSimple : public Downloader, public std::enable_shared_from_this< DownloaderSimple<AIO, Parser> >
{
    using State = StatusDownloader::State;

    using Loop = typename AIO::Loop;
    using ErrorEvent = typename AIO::ErrorEvent;

    using UriParseResult = typename Parser::UriParseResult;

    using AddrInfoEvent = typename AIO::AddrInfoEvent;
    using GetAddrInfoReq = typename AIO::GetAddrInfoReq;

    using TCPSocket = typename AIO::TCPSocket;
    using TCPSocketSimple = typename AIO::TCPSocketSimple;
    using ConnectEvent = typename AIO::ConnectEvent;
    using WriteEvent = typename AIO::WriteEvent;
    using DataEvent = typename AIO::DataEvent;
    using EndEvent = typename AIO::EndEvent;
    using ShutdownEvent = typename AIO::ShutdownEvent;

    using Timer = typename AIO::TimerHandle;
    using TimerEvent = typename AIO::TimerEvent;

    using FileReq = typename AIO::FileReq;
    using FileOpenEvent = typename AIO::FileOpenEvent;
    using FileWriteEvent = typename AIO::FileWriteEvent;
    using FileCloseEvent = typename AIO::FileCloseEvent;

    using FsReq = typename AIO::FsReq;

public:
    DownloaderSimple(std::shared_ptr<Loop> loop_, std::shared_ptr<OnTick> on_tick_, std::size_t backlog_ = 10)
        : loop{ std::move(loop_) },
          on_tick{ std::move(on_tick_) },
          backlog{backlog_}
    {}

    virtual bool run(const std::string&, const std::string&) override final;
    virtual void stop() override final { on_error("Abort."); }
    virtual const StatusDownloader& status() const override final { return m_status; }

    DownloaderSimple() = delete;
    DownloaderSimple(const DownloaderSimple&) = delete;
    DownloaderSimple(DownloaderSimple&&) = delete;
    DownloaderSimple& operator= (const DownloaderSimple&) = delete;
    DownloaderSimple& operator= (DownloaderSimple&&) = delete;
    virtual ~DownloaderSimple() = default;

protected:
    virtual std::shared_ptr<TCPSocket> create_socket(const std::string&) { return loop->template resource<TCPSocketSimple>(); }

private:
    std::shared_ptr<Loop> loop;
    std::shared_ptr<OnTick> on_tick;
    const std::size_t backlog;

    std::string fname;
    StatusDownloader m_status;
    std::unique_ptr<UriParseResult> uri_parsed;
    std::shared_ptr<GetAddrInfoReq> resolver;
    std::shared_ptr<TCPSocket> socket;
    std::shared_ptr<Timer> net_timer;
    std::unique_ptr<Parser> http_parser;
    std::shared_ptr<FileReq> file;

    bool receive_done = false;
    bool socket_connected = false;

    std::queue<DataChunk> buffer;
    bool file_openned = false;
    bool file_operation_started = false;
    std::size_t offset_file = 0;

    std::pair<bool, std::string> create_handles();
    void terminate_handles();
    void close_handles(std::function<void()>);
    void open_file(const std::string&fname);

    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    on_error_without_tick(String&& str)
    {
        m_status.state = StatusDownloader::State::Failed;
        m_status.state_str = std::forward<String>(str);
        terminate_handles();
    }

    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    on_error(String&& str)
    {
        on_error_without_tick( std::forward<String>(str) );
        on_tick->invoke( this->template shared_from_this() );
    }

    template< typename String >
    std::enable_if_t< std::is_convertible<String, std::string>::value, void>
    update_status(State state, String&& str)
    {
        m_status.state = state;
        m_status.state_str = std::forward<String>(str);
        on_tick->invoke( this->template shared_from_this() );
    }

    void on_resolve(const AddrInfoEvent&);
    void on_connect();
    void on_write_http_request();
    void on_read(std::unique_ptr<char[]>, std::size_t);
    void on_data(std::unique_ptr<char[]>, std::size_t);
    void on_write();

    std::pair< std::unique_ptr<char[]>, std::size_t > make_request() const;
};

/* -- implementation, because template( -- */

template< typename ErrorEvent >
inline std::string ErrorEvent2str(const ErrorEvent& err)
{
    return "Code => " + std::to_string(err.code()) + " Reason => " + err.what();
}

template< typename AIO, typename Parser >
bool DownloaderSimple<AIO, Parser>::run(const std::string& uri, const std::string& fname_)
{
    fname = fname_;
    m_status.state = State::Init;

    uri_parsed = Parser::uri_parse(uri);
    if (!uri_parsed)
    {
        m_status.state = State::Failed;
        m_status.state_str = "URI can`t parse";
        return false;
    }

    auto result = create_handles();
    if (!result.first)
    {
        m_status.state = State::Failed;
        m_status.state_str = std::move(result.second);
        return false;
    }

    auto self = this->template shared_from_this();

    resolver->template once<ErrorEvent>( [self](const auto& err, const auto&)
    {
        self->resolver.reset();
        std::string err_str = "Host <" + self->uri_parsed->host + "> can`t resolve. " + ErrorEvent2str(err);
        if ( self->m_status.state == State::OnTheGo )
            self->on_error( std::move(err_str) );
        else
            self->on_error_without_tick( std::move(err_str) );
    } );
    resolver->template once<AddrInfoEvent>( [self](const auto& event, const auto&)
    {
        self->resolver.reset();
        self->on_resolve(event);
    } );

    resolver->nodeAddrInfo(uri_parsed->host);

    if ( m_status.state == State::Init )
    {
        m_status.state = State::OnTheGo;
        m_status.state_str = "Resolve host <" + uri_parsed->host + ">...";
        return true;
    }
    return false;
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_resolve(const AddrInfoEvent& event)
{
    using namespace ::std::chrono_literals;

    const auto addr = AIO::addrinfo2IPAddress( event.data.get() );
    update_status(State::OnTheGo, "Host Resolved. Connect to <" + addr.ip + ">");

    auto self = this->template shared_from_this();
    socket->template once<ErrorEvent>( [self, addr](const auto& err, const auto&) { self->on_error("Host <" + addr.ip + "> can`t available. " + ErrorEvent2str(err) ); } );
    socket->template once<ConnectEvent>( [self](const auto&, const auto&) { self->on_connect(); } );
    net_timer->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error("Net_timer run failed! " + ErrorEvent2str(err) ); } );
    net_timer->template once<TimerEvent>( [self, addr](const auto&, const auto&) { self->on_error("Timeout connect to host <" + addr.ip + ">"); } );

    if (addr.v6)
        socket->connect6(addr.ip, uri_parsed->port);
    else
        socket->connect(addr.ip, uri_parsed->port);
    if ( m_status.state != State::Failed )
        net_timer->start(5s, 0s);
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_connect()
{
    using namespace ::std::chrono_literals;

    socket_connected = true;
    socket->clear();
    net_timer->template clear<TimerEvent>();
    net_timer->stop();

    update_status(State::OnTheGo, "Connected, write request.");

    auto self = this->template shared_from_this();
    socket->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error( "Request failed. " + ErrorEvent2str(err) ); } );
    socket->template once<WriteEvent>( [self](const auto&, const auto&) { self->on_write_http_request(); } );
    net_timer->template once<TimerEvent>( [self](const auto&, const auto&) { self->on_error("Timeout write request"); } );

    auto request = make_request();
    socket->write( std::move(request.first), request.second );
    if ( m_status.state != State::Failed )
        net_timer->start(5s, 0s);
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_write_http_request()
{
    using namespace ::std::placeholders;
    using namespace ::std::chrono_literals;

    socket->clear();
    net_timer->template clear<TimerEvent>();
    net_timer->stop();

    update_status(State::OnTheGo, "Write request done. Wait response.");

    auto self = this->template shared_from_this();
    socket->template once<ErrorEvent>( [self](const auto& err, const auto&) { self->on_error("Response read failed. " + ErrorEvent2str(err) ); } );
    socket->template once<EndEvent>( [self](const auto&, const auto&) { self->on_error("Connection it`s unexpecdly closed."); } );
    socket->template once<DataEvent>( [self](auto& event, const auto&)
    {
        self->socket->template clear<EndEvent>();
        self->socket->template once<EndEvent>( [self](const auto&, const auto&) { self->on_read(nullptr, 0); } );

        auto on_data = std::bind(&DownloaderSimple<AIO, Parser>::on_data, self, _1, _2);
        self->http_parser = Parser::create( std::move(on_data) );

        self->on_read( std::move(event.data), event.length );
    } );
    net_timer->template once<TimerEvent>( [self](const auto&, const auto&) { self->on_error("Timeout read response"); } );

    socket->read();
    if ( m_status.state != State::Failed )
        net_timer->start(5s, 0s);
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_read(std::unique_ptr<char[]> data, std::size_t length)
{
    using Result = typename Parser::ResponseParseResult::State;
    using namespace ::std::chrono_literals;

    net_timer->stop();
    auto result = http_parser->response_parse(std::move(data), length);
    if ( m_status.state == State::Failed )
        return;

    m_status.size = result.content_length;

    auto self = this->template shared_from_this();

    switch (result.state)
    {
    case Result::InProgress:
        socket->template once<DataEvent>( [self](auto& event, const auto&) { self->on_read(std::move(event.data), event.length); } );
        net_timer->start(5s, 0s);
        update_status(State::OnTheGo, "Data received.");
        break;

    case Result::Redirect:
        m_status.redirect_uri = std::move(result.redirect_uri);
        socket->stop();
        close_handles( [self]()
        {
            self->update_status(State::Redirect, "Redirect to <" + self->m_status.redirect_uri + ">");
        } );
        break;

    case Result::Done:
        receive_done = true;
        socket->stop();
        close_handles( [self]()
        {
            if ( !(self->file_openned) )
                self->update_status(State::Done, "Downloading complete");
        } );
        break;

    case Result::Error:
        on_error("Response parse failed. " + std::move(result.err_str) );
        break;
    }
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_data(std::unique_ptr<char[]> data, std::size_t length)
{
    m_status.downloaded += length;
    buffer.emplace(std::move(data), length);
    if ( buffer.size() >= backlog && socket->active() )
        socket->stop();

    if (!file)
        open_file(fname);

    if (file_openned && !file_operation_started)
    {
        file_operation_started = true;
        on_write();
    }
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_write()
{
    if ( buffer.empty() )
    {
        if (receive_done)
        {
            file->clear();
            auto self = this->template shared_from_this();
            file->template once<ErrorEvent>( [self](const auto& err, const auto&)
            {
                self->file_operation_started = false;
                self->file.reset();
                self->on_error("File <" + self->fname + "> close error! " + ErrorEvent2str(err) );
            } );
            file->template once<FileCloseEvent>( [self](const auto&, const auto&)
            {
                self->file_operation_started = false;
                self->file_openned = false;
                self->file->clear();
                if ( !(self->socket_connected) )
                    self->update_status(State::Done, "Downloading complete");
            } );
            file->close();
        } else
        {
            file_operation_started = false;
            if ( !(socket->active()) )
                socket->read();
        }
        return;
    }

    std::weak_ptr<DownloaderSimple> weak{ this->template shared_from_this() };
    file->template once<FileWriteEvent>( [weak](const auto& event, const auto&)
    {
        auto self = weak.lock();
        if (self)
        {
            self->offset_file += event.size;

            DataChunk& chunk = self->buffer.front();
            chunk.offset += event.size;
            if (chunk.offset == chunk.length)
                self->buffer.pop();

            self->on_write();
        }
    } );

    DataChunk& chunk = buffer.front();
    std::size_t chunk_available = chunk.length - chunk.offset;
    char* chunk_ptr = chunk.data.get() + chunk.offset;
    assert( chunk_available <= std::numeric_limits<unsigned int>::max() );
    file->write(chunk_ptr, chunk_available, offset_file);
}

template< typename AIO, typename Parser >
std::pair<bool, std::string> DownloaderSimple<AIO, Parser>::create_handles()
{
    socket = create_socket(uri_parsed->proto);
    if (!socket)
        return std::pair<bool, std::string>{false, "Socket can`t create"};

    net_timer = loop->template resource<Timer>();
    if (!net_timer)
    {
        socket->close();
        return std::pair<bool, std::string>{false, "Net timer can`t create"};
    }

    resolver = loop->template resource<GetAddrInfoReq>();
    if (!resolver)
    {
        socket->close();
        net_timer->close();
        return std::pair<bool, std::string>{false, "Resolver can`t create"};
    }

    return std::pair<bool, std::string>{true, ""};
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::terminate_handles()
{
    if (resolver)
    {
        resolver->clear();
        resolver->cancel();
    }
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
    if (file)
    {
        file->clear();
        if (file_operation_started)
            file->cancel();

        if (file_openned)
        {
            file->template once<FileCloseEvent>( [fs = loop->template resource<FsReq>(), fname = fname](const auto&, const auto&) { fs->unlink(fname); } );
            file->close();
        }
    }
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::close_handles(std::function<void()> cb)
{
    socket->clear();
    auto self = this->template shared_from_this();
    socket->template once<ShutdownEvent>( [self, cb = std::move(cb)](const auto&, const auto&)
    {
        self->socket_connected = false;
        self->socket->close();
        self->socket.reset();
        cb();
    } );
    socket->shutdown();

    net_timer->clear();
    net_timer->close();
    net_timer.reset();
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

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::open_file(const std::string& fname)
{
    file = loop->template resource<FileReq>();
    auto self = this->template shared_from_this();

    file->template once<ErrorEvent>( [self](const auto& err, const auto&)
    {
        self->file_operation_started = false;
        self->on_error("File <" + self->fname + "> can`t open! " + ErrorEvent2str(err) );
    } );

    file->template once<FileOpenEvent>( [self](const auto&, const auto&)
    {
        self->file_openned = true;
        self->file->template clear<ErrorEvent>();
        self->file->template once<ErrorEvent>( [self](const auto& err, const auto&)
        {
            self->file_operation_started = false;
            self->on_error("File <" + self->fname + "> write error! " + ErrorEvent2str(err) );
        } );
        self->on_write();
    } );

    file_operation_started = true;
    file->open(fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP);
}
