#pragma once

#include "downloader.h"

#include <string>
#include <memory>

class OnTick;
class Factory
{
public:
    virtual std::shared_ptr<Downloader> create(std::size_t job_id, const std::string& uri, const std::string& fname) = 0;
    virtual void set_OnTick(std::shared_ptr<OnTick>) = 0;
    virtual ~Factory() = default;
};
