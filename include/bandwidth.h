#pragma once

#include <memory>

namespace bandwidth {

class Stream
{
public:
    virtual void set_buffer(std::size_t) noexcept = 0;
    virtual std::size_t available() const noexcept = 0;
    virtual void transfer(std::size_t) = 0;
    virtual ~Stream() = default;
};

class Controller
{
public:
    virtual void add_stream(std::weak_ptr<Stream>) = 0;
    virtual void remove_stream(std::weak_ptr<Stream>) = 0;
    virtual void shedule_transfer() = 0;
    virtual ~Controller() = default;
};

} // namespace bandwidth
