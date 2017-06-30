#pragma once

#include <gmock/gmock.h>
#include "on_tick.h"

class OnTickMock : public OnTick
{
public:
    virtual void invoke(std::shared_ptr<Downloader> d) override { invoke_( d.get() ); }
    MOCK_METHOD1( invoke_, void(Downloader*) );
};
