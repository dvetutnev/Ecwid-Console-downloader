#pragma once

#include "task.h"
#include "downloader.h"

#include <list>

class Job
{
public:
    Job( std::shared_ptr<Task> t, std::shared_ptr<Downloader> d, std::size_t redirect_count_ = 0 )
        : task{ std::move(t) },
          downloader{ std::move(d) },
          redirect_count{redirect_count_}
    {}

    std::shared_ptr<Task> task;
    std::shared_ptr<Downloader> downloader;
    std::size_t redirect_count;
};

inline bool operator== (const Job& a, const Job& b)
{
    return a.task == b.task && a.downloader == b.downloader && a.redirect_count == b.redirect_count;
}
