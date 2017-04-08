#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "on_tick.h"

class OnTickMock : public OnTick
{
public:
    MOCK_METHOD1(invoke, void(std::shared_ptr<Downloader>));
};
