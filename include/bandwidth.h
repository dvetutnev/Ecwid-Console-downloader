#pragma once

#include <memory>
#include <chrono>
#include <list>

namespace bandwidth {

class Stream
{
public:
    virtual void set_buffer(std::size_t) noexcept = 0;
    virtual std::size_t available() const noexcept = 0;
    virtual void transfer(std::size_t) = 0;
    virtual ~Stream() = default;
};

class Time
{
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

public:
    Time()
        : last{ Clock::now() }
    {}

    virtual Duration elapsed() noexcept
    {
        auto current = Clock::now();
        auto diff = std::chrono::duration_cast<Duration>(current - last);
        last = current;
        return diff;
    }

    virtual ~Time() = default;

private:
    Clock::time_point last;
};

class Controller
{
public:
    using StreamsList = std::list< std::weak_ptr<Stream> >;
    using StreamConnection = StreamsList::iterator;

    virtual StreamConnection add_stream(std::weak_ptr<Stream>) = 0;
    virtual void remove_stream(StreamConnection) = 0;
    virtual void shedule_transfer() = 0;
    virtual ~Controller() = default;
};

} // namespace bandwidth
