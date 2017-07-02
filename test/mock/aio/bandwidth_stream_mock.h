#pragma once

#include <gmock/gmock.h>
#include "aio/bandwidth.h"

namespace aio {
namespace bandwidth {

struct StreamMock : public Stream
{
    virtual void set_buffer(std::size_t size) noexcept { set_buffer_(size); }
    MOCK_METHOD1( set_buffer_, void(std::size_t) );
    virtual std::size_t available() const noexcept { return available_(); }
    MOCK_CONST_METHOD0( available_, std::size_t() );
    MOCK_METHOD1( transfer, void(std::size_t) );
};

} // namespace bandwidth
} // namespace aio
