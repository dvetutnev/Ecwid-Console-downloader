#pragma once

#include <gmock/gmock.h>
#include "aio/bandwidth.h"

namespace aio {
namespace bandwidth {

struct TimeMock : public Time
{
    virtual std::chrono::milliseconds elapsed() noexcept { return elapsed_(); }
    MOCK_METHOD0( elapsed_, std::chrono::milliseconds() );
};

} // namespace bandwidth
} // namespace aio
