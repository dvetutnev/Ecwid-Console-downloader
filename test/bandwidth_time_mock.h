#pragma once

#include <gmock/gmock.h>
#include "bandwidth.h"

namespace bandwidth {

struct TimeMock : public Time
{
    virtual std::chrono::milliseconds elapsed() noexcept { return elapsed_(); }
    MOCK_METHOD0( elapsed_, std::chrono::milliseconds() );
};

} // namespace bandwidth
