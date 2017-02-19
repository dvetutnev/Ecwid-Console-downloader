#pragma once

#include "downloader.h"

class Factory
{
public:
    virtual std::shared_ptr<Downloader> create(const Task&) = 0;
    virtual void set_OnTick(std::function<void(std::shared_ptr<Downloader>)>) = 0;
    virtual ~Factory() = default;
};
