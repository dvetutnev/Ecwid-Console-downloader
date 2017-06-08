#pragma once

#include <gmock/gmock.h>
#include "bandwidth.h"

namespace bandwidth {

struct ControllerMock : public Controller
{
    MOCK_METHOD1( add_stream, void(std::weak_ptr<Stream>) );
    MOCK_METHOD1( remove_stream, void(std::weak_ptr<Stream>) );
    MOCK_METHOD0( shedule_transfer, void() );
};

} // namespace bandwidth
