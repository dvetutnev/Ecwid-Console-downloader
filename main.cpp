#include "program_options.h"
#include "task_simple.h"
#include "factory_simple.h"
#include "aio/bandwidth_controller.h"
#include "aio/factory_tcp_bandwidth.h"
#include "dashboard_simple.h"
#include "on_tick_simple.h"
#include <uvw/signal.hpp>

#include <fstream>
#include <iostream>
#include <chrono>

using namespace std;

int main(int argc, char *argv[])
{
    const auto program_options = parse_program_options(argc, argv);

    ifstream task_stream{program_options.task_fname};
    if ( !task_stream.is_open() )
    {
        cout << "Can`t open <" << program_options.task_fname << ">, break." << endl;
        return 1;
    }

    TaskListSimple task_list{task_stream, program_options.path};
    DashboardSimple dashboard{};

    auto loop = uvw::Loop::getDefault();
    auto controller = make_shared< aio::bandwidth::ControllerSimple<AIO_UVW> >( loop, program_options.limit, make_unique<aio::bandwidth::Time>() );
    auto factory_socket = make_shared<aio::FactoryTCPSocketBandwidth>(loop, controller);
    auto factory = make_shared<FactorySimple>(loop, dashboard, factory_socket);

    std::list<Job> job_list;
    auto on_tick = make_shared<OnTickSimple>(job_list, factory, task_list, dashboard);
    factory->set_OnTick(on_tick);

    for (size_t i = 1; i <= program_options.concurrency; i++)
    {
        auto task = task_list.get();
        if (!task)
            break;

        Job job{task->fname};
        job.downloader = factory->create(job.id, task->uri, task->fname);
        if ( !(job.downloader) )
            continue;

        job_list.push_back( move(job) );
    }

    auto signal_handle = loop->resource<uvw::SignalHandle>();
    auto sig_handler = [&factory, &job_list](const auto&, auto& handle)
    {
        cout << "Break" << endl;
        std::list< shared_ptr<Downloader> > downloader_list;
        for (auto it = begin(job_list); it != end(job_list); ++it)
            downloader_list.push_back(it->downloader);
        factory.reset();
        for (auto it = begin(downloader_list); it != end(downloader_list); ++it)
            (*it)->stop();
        handle.close();
    };
    signal_handle->once<uvw::SignalEvent>(sig_handler);
    signal_handle->oneShot(SIGINT);

    using Clock = chrono::steady_clock;
    using Duration = chrono::seconds;
    auto start_time = Clock::now();

    loop->run();

    auto elapsed = chrono::duration_cast<Duration>(Clock::now() - start_time);
    auto status = dashboard.status();
    cout << "---------------" << endl;
    cout << "Done tasks: " << status.first << ", total downloaded: " << status.second << ", time elapsed: " << elapsed.count() << " seconds" << endl;

    return 0;
}
