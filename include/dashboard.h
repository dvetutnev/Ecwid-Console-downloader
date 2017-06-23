#pragma once

#include "downloader.h"

class Dashboard
{
public:
    virtual void update(std::shared_ptr<const Task>, const StatusDownloader&) = 0;
    virtual ~Dashboard() = default;
};
