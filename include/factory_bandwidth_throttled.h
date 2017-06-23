#pragma once

#include "factory.h"
#include "downloader_bandwidth_throttled.h"
#include "aio_uvw.h"
#include "bandwidth.h"
#include "http.h"

class FactoryBandwidthThrottled : public Factory
{
    using Controller = ::bandwidth::Controller;
public:
    FactoryBandwidthThrottled(std::shared_ptr<AIO_UVW::Loop> loop_, std::shared_ptr<Controller> controller_)
        : loop{ std::move(loop_) },
          controller{ std::move(controller_) }
    {}

    virtual std::shared_ptr<Downloader> create(const Task& task) override
    {
        auto downloader = std::make_shared< DownloaderBandwidthThrottled<AIO_UVW, HttpParser> >(loop, on_tick, controller);
        return ( downloader->run(task) ) ? downloader : nullptr;
    }
    virtual void set_OnTick(std::shared_ptr<OnTick> on_tick_) override { on_tick = std::move(on_tick_); }

    FactoryBandwidthThrottled() = delete;
    FactoryBandwidthThrottled(const FactoryBandwidthThrottled&) = delete;
    FactoryBandwidthThrottled& operator= (const FactoryBandwidthThrottled&) = delete;
    virtual ~FactoryBandwidthThrottled() = default;

private:
    std::shared_ptr<AIO_UVW::Loop> loop;
    std::shared_ptr<Controller> controller;
    std::shared_ptr<OnTick> on_tick;
};
