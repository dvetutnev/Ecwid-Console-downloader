#pragma once

#include <memory>

class Downloader;
class OnTick
{
public:
    virtual void invoke(std::shared_ptr<Downloader>) = 0;
    virtual ~OnTick() = default;
};
