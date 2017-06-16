#pragma once

#include "bandwidth.h"

#include <list>

namespace bandwidth {

class ControllerSimple final : public Controller
{
public:
    ControllerSimple(std::size_t limit_, std::unique_ptr<Time> time_)
        : limit{limit_},
          time{ std::move(time_) }
    {

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

    std::list< std::weak_ptr<Stream> > streams;
};

} // namespace bandwidth
