#pragma once

#include "task.h"
#include "downloader.h"

#include <list>

class Job
{
public:
    Job()
        : task{},
          downloader{},
          redirect_count{0}
    {}

    std::shared_ptr<Task> task;
    std::shared_ptr<Downloader> downloader;
    std::size_t redirect_count;
};

using JobList = std::list<Job>;

inline bool operator== (const Job& a, const Job& b)
{
    return a.task == b.task && a.downloader == b.downloader && a.redirect_count == b.redirect_count;
}
