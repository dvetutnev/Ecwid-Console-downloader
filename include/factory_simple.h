#pragma once

#include "factory.h"
#include "dashboard.h"
#include "aio/factory_tcp.h"
#include "downloader_simple.h"
#include "aio_uvw.h"
#include "http.h"

class FactorySimple : public Factory
{
public:
    FactorySimple(std::shared_ptr<AIO_UVW::Loop> loop_, Dashboard& dashboard_, std::shared_ptr<aio::FactoryTCPSocket> factory_socket_)
        : loop{ std::move(loop_) },
          dashboard{dashboard_},
          factory_socket{ std::move(factory_socket_) }
    {}

    virtual std::shared_ptr<Downloader> create(std::size_t job_id, const std::string& uri, const std::string& fname) override
    {
        auto downloader = std::make_shared< DownloaderSimple<AIO_UVW, HttpParser> >(loop, on_tick, factory_socket);
        bool runned = downloader->run(uri, fname);
        dashboard.update(job_id, downloader->status());
        return (runned) ? downloader : nullptr;
    }
    virtual void set_OnTick(std::shared_ptr<OnTick> on_tick_) override { on_tick = std::move(on_tick_); }

    FactorySimple() = delete;
    FactorySimple(const FactorySimple&) = delete;
    FactorySimple(FactorySimple&&) = delete;
    FactorySimple& operator= (const FactorySimple&) = delete;
    FactorySimple& operator= (FactorySimple&&) = delete;

    virtual ~FactorySimple() = default;

private:
    std::shared_ptr<AIO_UVW::Loop> loop;
    Dashboard& dashboard;
    std::shared_ptr<aio::FactoryTCPSocket> factory_socket;
    std::shared_ptr<OnTick> on_tick;
};
