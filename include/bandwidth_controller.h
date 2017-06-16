#pragma once

#include "bandwidth.h"

#include <list>
#include <cassert>
#include <exception>

namespace bandwidth {

template< typename AIO >
class ControllerSimple final : public Controller, public std::enable_shared_from_this< ControllerSimple<AIO> >
{
    using Loop = typename AIO::Loop;
    using TimerHandle = typename AIO::TimerHandle;
    using TimerEvent = typename AIO::TimerEvent;

public:
    ControllerSimple(std::shared_ptr<Loop>loop, std::size_t limit_, std::unique_ptr<Time> time_)
        : limit{limit_},
          time{ std::move(time_) },
          timer{ loop->template resource<TimerHandle>() }
    {
        if (!timer)
            throw std::runtime_error{"ControllerSimple<AIO>: AIO::TimerHandle can`t create!"};
    }

    virtual void add_stream(std::weak_ptr<Stream>) override;
    virtual void remove_stream(std::weak_ptr<Stream>) override;
    virtual void shedule_transfer() override;

    ControllerSimple() = delete;
    ControllerSimple(const ControllerSimple&) = delete;
    ControllerSimple& operator= (const ControllerSimple) = delete;
    virtual ~ControllerSimple() = default;

private:
    const std::size_t limit;
    std::unique_ptr<Time> time;
    std::shared_ptr<TimerHandle> timer;

    std::list< std::weak_ptr<Stream> > streams;
};

/* Implementation */

template< typename AIO >
void ControllerSimple<AIO>::add_stream(std::weak_ptr<Stream> weak)
{
    auto stream = weak.lock();
    if (stream)
    {
        stream->set_buffer( limit * 4 );
        streams.push_back(weak);
    }
}

template< typename AIO >
void ControllerSimple<AIO>::remove_stream(std::weak_ptr<Stream>)
{

}

template< typename AIO >
void ControllerSimple<AIO>::shedule_transfer()
{
    const auto elapsed = time->elapsed().count();
    assert(elapsed >= 0);
    std::size_t total_to_transfer = ( limit * static_cast<size_t>(elapsed) ) / 1000;

    std::size_t pending_streams = streams.size();
    if (pending_streams == 0)
        return;

    do {
        std::size_t chunk = std::max( total_to_transfer / pending_streams, 1ul );
        pending_streams = 0;
        for (auto it = begin(streams); it != end(streams); ++it)
        {
            auto stream = it->lock();
            std::size_t available = stream->available();
            std::size_t to_transfer = std::min(available, chunk);
            if (to_transfer == 0)
                continue;

            stream->transfer(to_transfer);
            total_to_transfer -= to_transfer;
            if (available - to_transfer > 0)
                pending_streams++;
        }
    } while (total_to_transfer > 0 && pending_streams > 0);
}

} // namespace bandwidth
