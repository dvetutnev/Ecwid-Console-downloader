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
public:
    virtual std::chrono::milliseconds elapsed() noexcept = 0;
    virtual ~Time() = default;
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
