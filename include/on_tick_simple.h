#pragma once

#include "on_tick.h"
#include "job.h"
#include "factory.h"
#include "task.h"
#include "dashboard.h"

#include <list>

class OnTickSimple : public OnTick
{
    using JobList = std::list<Job>;
    using It = JobList::iterator;
    using ConstIt = JobList::const_iterator;

public:
    OnTickSimple(JobList& job_list_, std::weak_ptr<Factory> factory, TaskList& task_list_, Dashboard& dashboard_, std::size_t max_redirect_ = 10)
        : job_list{job_list_},
          weak_factory{factory},
          task_list{task_list_},
          dashboard{dashboard_},
          max_redirect{max_redirect_}
    {}

    virtual void invoke(std::shared_ptr<Downloader>) override;

    OnTickSimple() = delete;
    OnTickSimple(const OnTickSimple&) = delete;
    OnTickSimple(OnTickSimple&&) = delete;
    OnTickSimple& operator= (const OnTickSimple&) = delete;
    OnTickSimple& operator= (OnTickSimple&&) = delete;

    virtual ~OnTickSimple() = default;

private:
    void next_task(const ConstIt);
    void redirect(It, const std::string&);
    It find_job(Downloader*);

    JobList& job_list;
    std::weak_ptr<Factory> weak_factory;
    TaskList& task_list;
    Dashboard& dashboard;
    const std::size_t max_redirect;
};
