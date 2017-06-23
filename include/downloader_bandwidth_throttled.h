#pragma once

#include "downloader_simple.h"
#include "bandwidth.h"

template< typename AIO, typename Parser >
class DownloaderBandwidthThrottled : public DownloaderSimple<AIO, Parser>
{
    using Loop = typename AIO::Loop;
    using TCPSocket = typename AIO::TCPSocket;
    using TCPSocketBandwidth = typename AIO::TCPSocketBandwidth;

    using Controller = ::bandwidth::Controller;

public:
    DownloaderBandwidthThrottled(std::shared_ptr<Loop> loop, std::shared_ptr<OnTick> on_tick, std::shared_ptr<Controller> controller_, std::size_t backlog = 10)
        : DownloaderSimple<AIO, Parser>{loop, std::move(on_tick), backlog},
          loop{ std::move(loop) },
          controller{ std::move(controller_) }
    {}

protected:
    virtual std::shared_ptr<TCPSocket> create_socket(const std::string&) override;

private:
    std::shared_ptr<Loop> loop;
    std::shared_ptr<Controller> controller;
};

template< typename AIO, typename Parser >
std::shared_ptr< typename AIO::TCPSocket > DownloaderBandwidthThrottled<AIO, Parser>::create_socket(const std::string& proto)
{
    auto socket = DownloaderSimple<AIO, Parser>::create_socket(proto);
    if (!socket)
        return nullptr;
    return loop->template resource<TCPSocketBandwidth>( controller, std::move(socket) );
}
