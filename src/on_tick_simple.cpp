#include "on_tick_simple.h"

#include <algorithm>
#include <exception>

using ::std::shared_ptr;
using ::std::string;
using ::std::move;
using ::std::begin;
using ::std::end;
using ::std::find_if;
using ::std::runtime_error;

void OnTickSimple::invoke(shared_ptr<Downloader> downloader)
{
    using State = StatusDownloader::State;

    auto job_it = find_job( downloader.get() );
    const auto status = downloader->status();

    dashboard.update(job_it->id, status);

    switch (status.state)
    {
    case State::Done:
    case State::Failed:
        next_task(job_it);
        break;
    case State::Redirect:
        redirect(job_it, status.redirect_uri);
        break;
    default:
        break;
    }
}

void OnTickSimple::next_task(const ConstIt job_it)
{
    job_list.erase(job_it);

    auto factory = weak_factory.lock();
    if ( !factory )
        return;

    for (;;)
    {
        auto task = task_list.get();
        if ( !task )
            return;

        Job job{task->fname};
        job.downloader = factory->create(job.id, task->uri, task->fname);
        if ( !job.downloader )
            continue;

        job_list.push_back( move(job) );
        break;
    }
}

void OnTickSimple::redirect(It job_it, const string& uri)
{
    if ( ++(job_it->redirect_count) > max_redirect )
    {
        StatusDownloader status;
        status.state = StatusDownloader::State::Failed;
        status.state_str = "Maximum count redirect";
        dashboard.update(job_it->id, status);

        next_task(job_it);
        return;
    }

    auto factory = weak_factory.lock();
    if (factory)
    {
        job_it->downloader = factory->create(job_it->id, uri, job_it->fname);
        if ( !(job_it->downloader) )
            next_task(job_it);

    } else
    {
        job_list.erase(job_it);
    }
}

OnTickSimple::It OnTickSimple::find_job(Downloader* downloader)
{
    auto it = find_if( begin(job_list), end(job_list),
                            [&downloader](const Job& job) { return job.downloader.get() == downloader; } );
    if ( it == end(job_list) )
        throw runtime_error{"OnTickSimple => invalid Downloader"};
    return it;
}
