#include "task_simple.h"
#include "factory_simple.h"
#include "factory_bandwidth_throttled.h"
#include "aio/bandwidth_controller.h"
#include "dashboard_simple.h"
#include "on_tick_simple.h"

#include <fstream>
#include <iostream>

using namespace std;

int main(int argc, char *argv[])
{
    const string task_fname = "../Ecwid-Console-downloader.txt";
    const size_t concurrency = 2;
    const size_t limit = 3000'000;

    ifstream task_stream{task_fname};
    if ( !task_stream.is_open() )
    {
        cout << "Can`t open <" << task_fname << ">, break." << endl;
        return 1;
    }

    TaskListSimple task_list{task_stream, "./"};
    DashboardSimple dashboard{};

    auto loop = AIO_UVW::Loop::getDefault();
    auto controller = make_shared< ::aio::bandwidth::ControllerSimple<AIO_UVW> >( loop, limit, make_unique<::aio::bandwidth::Time>() );
    auto factory = make_shared<FactoryBandwidthThrottled>(loop, dashboard, controller);

    std::list<Job> job_list;
    auto on_tick = make_shared<OnTickSimple>(job_list, factory, task_list, dashboard);
    factory->set_OnTick(on_tick);

    for (size_t i = 1; i <= concurrency; i++)
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

    loop->run();

    return 0;
}
