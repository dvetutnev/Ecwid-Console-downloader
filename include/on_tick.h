#pragma once

#include "downloader.h"

class OnTick
{
public:
    virtual void invoke(std::shared_ptr<Downloader>) = 0;
    virtual ~OnTick() = default;
};
