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

    virtual StreamConnection add_stream(std::weak_ptr<Stream>) override;
    virtual void remove_stream(StreamConnection) override;
    virtual void shedule_transfer() override;

    ControllerSimple() = delete;
    ControllerSimple(const ControllerSimple&) = delete;
    ControllerSimple(ControllerSimple&&) = delete;
    ControllerSimple& operator= (const ControllerSimple&) = delete;
    ControllerSimple& operator= (ControllerSimple&&) = delete;
    virtual ~ControllerSimple() = default;

private:
    const std::size_t limit;
    std::unique_ptr<Time> time;
    std::shared_ptr<TimerHandle> timer;

    StreamsList streams;
    bool sheduled  = false;

    void transfer();
    void defer_transfer();
};

/* Implementation */

template< typename AIO >
Controller::StreamConnection ControllerSimple<AIO>::add_stream(std::weak_ptr<Stream> weak)
{
    auto stream = weak.lock();
    if (!stream)
        return std::end(streams);

    stream->set_buffer( limit * 4 );
    return streams.insert(std::end(streams), weak);
}

template< typename AIO >
void ControllerSimple<AIO>::remove_stream(StreamConnection conn)
{
    std::weak_ptr<Stream> null_ptr{};
    std::swap(*conn, null_ptr);
}

template< typename AIO >
void ControllerSimple<AIO>::shedule_transfer()
{
    if (sheduled)
        return;
    transfer();
}

template< typename AIO >
void ControllerSimple<AIO>::transfer()
{
    sheduled = false;
    std::size_t pending_streams = streams.size();
    if (pending_streams == 0)
        return;

    const auto elapsed = time->elapsed().count();
    assert(elapsed >= 0);
    std::size_t total_to_transfer = ( limit * static_cast<size_t>(elapsed) ) / 1000;
    if (total_to_transfer == 0)
    {
        defer_transfer();
        return;
    }

    do {
        std::size_t chunk = std::max( total_to_transfer / pending_streams, 1ul );
        pending_streams = 0;

        auto it = std::begin(streams);
        const auto it_end = std::cend(streams);
        while (it != it_end)
        {
            auto stream = it->lock();
            if (stream)
            {
                ++it;

                std::size_t available = stream->available();
                std::size_t to_transfer = std::min(available, chunk);
                if (to_transfer == 0)
                    continue;

                stream->transfer(to_transfer);
                total_to_transfer -= to_transfer;
                if (available - to_transfer > 0)
                    pending_streams++;

            } else
            {
                auto rm_it = it++;
                streams.erase(rm_it);
            }
        }
    } while (total_to_transfer > 0 && pending_streams > 0);

    if (pending_streams > 0)
        defer_transfer();
}

template< typename AIO >
void ControllerSimple<AIO>::defer_transfer()
{
    sheduled = true;
    timer->template once<TimerEvent>( [self = this->template shared_from_this()](const auto&, const auto&) { self->transfer(); } );
    timer->start( std::chrono::milliseconds{50}, std::chrono::milliseconds{0} );
}

} // namespace bandwidth
