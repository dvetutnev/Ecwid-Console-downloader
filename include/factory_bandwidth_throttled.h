#pragma once

#include "factory.h"
#include "dashboard.h"
#include "downloader_bandwidth_throttled.h"
#include "aio_uvw.h"
#include "aio/bandwidth.h"
#include "http.h"

class FactoryBandwidthThrottled : public Factory
{
    using Controller = ::bandwidth::Controller;
public:
    FactoryBandwidthThrottled(std::shared_ptr<AIO_UVW::Loop> loop_, Dashboard& dashboard_, std::shared_ptr<Controller> controller_)
        : loop{ std::move(loop_) },
          dashboard{dashboard_},
          controller{ std::move(controller_) }
    {}

    virtual std::shared_ptr<Downloader> create(std::size_t job_id, const std::string& uri, const std::string& fname) override
    {
        auto downloader = std::make_shared< DownloaderBandwidthThrottled<AIO_UVW, HttpParser> >(loop, on_tick, controller);
        bool runned = downloader->run(uri, fname);
        dashboard.update(job_id, downloader->status());
        return (runned) ? downloader : nullptr;
    }
    virtual void set_OnTick(std::shared_ptr<OnTick> on_tick_) override { on_tick = std::move(on_tick_); }

    FactoryBandwidthThrottled() = delete;
    FactoryBandwidthThrottled(const FactoryBandwidthThrottled&) = delete;
    FactoryBandwidthThrottled(FactoryBandwidthThrottled&&) = delete;
    FactoryBandwidthThrottled& operator= (const FactoryBandwidthThrottled&) = delete;
    FactoryBandwidthThrottled& operator= (FactoryBandwidthThrottled&&) = delete;
    virtual ~FactoryBandwidthThrottled() = default;

private:
    std::shared_ptr<AIO_UVW::Loop> loop;
    Dashboard& dashboard;
    std::shared_ptr<Controller> controller;
    std::shared_ptr<OnTick> on_tick;
};
