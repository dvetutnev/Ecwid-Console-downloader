#pragma once

#include <gmock/gmock.h>
#include "factory.h"

class FactoryMock : public Factory
{
public:
    MOCK_METHOD3( create, std::shared_ptr<Downloader>(std::size_t, const std::string&, const std::string&) );
    MOCK_METHOD1( set_OnTick, void(std::shared_ptr<OnTick>) );
};
