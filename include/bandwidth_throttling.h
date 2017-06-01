#pragma once

#include <memory>

class BandwidthThrottled
{
public:
    virtual void set_buffer(std::size_t) noexcept = 0;
    virtual std::size_t available() const noexcept = 0;
    virtual void transfer(std::size_t) = 0;
    virtual ~BandwidthThrottled() = default;
};

class ControllerBandwidthThrottling
{
public:
    virtual void add_stream(std::weak_ptr<BandwidthThrottled>) = 0;
    virtual void remove_stream(std::weak_ptr<BandwidthThrottled>) = 0;
    virtual void shedule_transfer() = 0;
    virtual ~ControllerBandwidthThrottling() = default;
};
