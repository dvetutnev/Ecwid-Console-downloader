#pragma once

#include "downloader.h"

class Dashboard
{
public:
    virtual void update(std::size_t, const StatusDownloader&) = 0;
    virtual ~Dashboard() = default;
};
