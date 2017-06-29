#pragma once

#include "downloader.h"
#include "task.h"

#include <memory>

class OnTick;
class Factory
{
public:
    virtual std::shared_ptr<Downloader> create(const Task&) = 0;
    virtual void set_OnTick(std::shared_ptr<OnTick>) = 0;
    virtual ~Factory() = default;
};
