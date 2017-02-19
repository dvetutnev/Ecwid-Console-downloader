#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "factory.h"

class FactoryMock : public Factory
{
public:
    MOCK_METHOD1( create, std::shared_ptr<Downloader>(const Task&) );
    MOCK_METHOD1( set_OnTick, void(std::function<void(std::shared_ptr<Downloader>)>) );
};
