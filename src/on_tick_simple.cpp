#include "on_tick_simple.h"

#include <algorithm>
#include <exception>

using namespace std;

void OnTickSimple::invoke(shared_ptr<Downloader> downloader)
{
    auto job_it = find_job( downloader.get() );
    const auto status = downloader->status();

    dashboard.update(job_it->task, status);

    switch (status.state)
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

void OnTickSimple::next_task(const JobContainer::iterator job_it)
{
    job_container.erase(job_it);
    auto factory = weak_factory.lock();
    if ( !factory )
        return;

    for (;;)
    {
        auto task = task_list.get();
        if (!task)
            return;

        auto downloader = factory->create(*task);
        if (!downloader)
            continue;

        job_container.emplace_back(task, downloader);
        break;
    }
}

void OnTickSimple::redirect(JobContainer::iterator job_it)
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

OnTickSimple::JobContainer::iterator OnTickSimple::find_job(Downloader* downloader)
{
    auto it = std::find_if( std::begin(job_container), std::end(job_container),
                            [&downloader](const Job& job) { return job.downloader.get() == downloader; } );
    if ( it == std::end(job_container) )
        throw std::runtime_error{"OnTickSimple => invalid Downloader"};
    return it;
}
