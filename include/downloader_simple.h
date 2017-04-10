#pragma once

#include "downloader.h"
#include "on_tick.h"

template< typename AIO, typename Parser >
class DownloaderSimple : public Downloader, public std::enable_shared_from_this< DownloaderSimple<AIO, Parser> >
{
    using Loop = typename AIO::Loop;
    using ErrorEvent = typename AIO::ErrorEvent;
    using AddrInfoEvent = typename AIO::AddrInfoEvent;
    using GetAddrInfoReq = typename AIO::GetAddrInfoReq;
    using UriParseResult = typename Parser::UriParseResult;

public:
    DownloaderSimple(Loop& loop_, std::shared_ptr<OnTick> on_tick_)
        : loop{loop_},
          on_tick{on_tick_}
    {}

    virtual bool run(const Task&) override final;
    virtual void stop() override final;
    virtual const StatusDownloader& status() const override final;

private:
    Loop& loop;
    std::shared_ptr<OnTick> on_tick;

    StatusDownloader m_status;
    std::unique_ptr<UriParseResult> uri_parsed;

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
    return "Code => " + std::to_string(err.code()) + std::string{" Reason => "} + err.what();
}

template< typename AIO, typename Parser >
bool DownloaderSimple<AIO, Parser>::run(const Task& task)
{
    m_status.state = StatusDownloader::State::Init;
    uri_parsed = Parser::uri_parse(task.uri);
    if (!uri_parsed)
        return false;

    auto resolver = loop.template resource<GetAddrInfoReq>();
    /*TODO: self from lambda */
    resolver->template once<ErrorEvent>( [self = this->template shared_from_this()](const auto& err, auto&)
    {
        std::string err_str = "Can`t resolve " + self->uri_parsed->host + " " + ErrorEvent2str(err);
        if ( self->m_status.state == StatusDownloader::State::OnTheGo )
            self->on_error( std::move(err_str) );
        else
            self->on_error_without_tick( std::move(err_str) );
    } );
    resolver->template once<AddrInfoEvent>( [self = this->template shared_from_this()](const auto& event, auto&) { self->on_resolve(event); } );
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
const StatusDownloader& DownloaderSimple<AIO, Parser>::status() const
{
    return m_status;
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
    on_tick->invoke( this->template shared_from_this() );
}

template< typename AIO, typename Parser >
void DownloaderSimple<AIO, Parser>::on_resolve(const AddrInfoEvent& event)
{

}
