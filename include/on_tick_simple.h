#pragma once

#include <algorithm>
#include <exception>

#include "on_tick.h"
#include "factory.h"
#include "job.h"

template<typename JobContainer>
class OnTickSimple : public OnTick
{
public:
    OnTickSimple() = delete;
    OnTickSimple(const OnTickSimple&) = delete;
    OnTickSimple& operator= (const OnTickSimple) = delete;

    OnTickSimple(JobContainer& job_container_, std::weak_ptr<Factory> factory, TaskList& task_list_, std::size_t max_redirect_ = 10)
        : job_container{job_container_},
          weak_factory{factory},
          task_list{task_list_},
          max_redirect{max_redirect_}
    {}

    virtual void operator() (std::shared_ptr<Downloader>) override;

    virtual ~OnTickSimple() = default;

private:
    void next_task(const typename JobContainer::iterator&);
    void redirect(typename JobContainer::iterator&);
    typename JobContainer::iterator find_job(Downloader*);

    JobContainer& job_container;
    std::weak_ptr<Factory> weak_factory;
    TaskList& task_list;
    const std::size_t max_redirect;
};


/* -- implementation, because template( -- */

template<typename T>
void OnTickSimple<T>::operator()(std::shared_ptr<Downloader> downloader)
{
    auto job_it = find_job( downloader.get() );
    switch ( downloader->status().state )
    {
    case StatusDownloader::State::Done:
    case StatusDownloader::State::Failed:
        next_task(job_it);
        break;
    case StatusDownloader::State::Redirect:
        redirect(job_it);
        break;
    default:
        break;
    }
}

template<typename T>
void OnTickSimple<T>::next_task(const typename T::iterator& job_it)
{
    job_container.erase(job_it);
    auto factory = weak_factory.lock();
    if ( !factory )
        return;

    Job next_job;
    do
    {
        next_job.task = task_list.get();
        if ( !next_job.task )
            return;

        next_job.downloader = factory->create(*next_job.task);
    } while( !next_job.downloader );

    job_container.insert( std::end(job_container), next_job );
}

template<typename T>
void OnTickSimple<T>::redirect(typename T::iterator& job_it)
{
    if ( ++(job_it->redirect_count) > max_redirect)
    {
        next_task(job_it);
        return;
    }

    auto factory = weak_factory.lock();
    if (factory)
    {
        auto uri = job_it->downloader->status().redirect_uri;
        auto fname = job_it->task->fname;
        Task task{ std::move(uri), std::move(fname) };
        auto downloader = factory->create(task);
        if (downloader)
        {
            job_it->task->uri = task.uri;
            job_it->downloader = downloader;
        } else
        {
            next_task(job_it);
        }
    } else
    {
        job_container.erase(job_it);
    }
}

template<typename T>
typename T::iterator OnTickSimple<T>::find_job(Downloader* downloader)
{
    typename T::iterator it = std::find_if( std::begin(job_container), std::end(job_container),
                            [&downloader](const Job& job) { return job.downloader.get() == downloader; } );
    if ( it == std::end(job_container) )
        throw std::runtime_error{"OnTickSimple => invalid Downloader"};
    return it;
}
