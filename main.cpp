#include "task_simple.h"
#include "factory_simple.h"
#include "factory_bandwidth_throttled.h"
#include "bandwidth_controller.h"
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

    auto loop = AIO_UVW::Loop::getDefault();
    auto controller = make_shared< ::bandwidth::ControllerSimple<AIO_UVW> >( loop, limit, make_unique<::bandwidth::Time>() );
    auto factory = make_shared<FactoryBandwidthThrottled>(loop, controller);
    DashboardSimple dashboard{};
    std::list<Job> job_list;
    auto on_tick = make_shared<OnTickSimple>(job_list, factory, task_list, dashboard);
    factory->set_OnTick(on_tick);

    for (size_t i = 1; i <= concurrency; i++)
    {
        auto task = task_list.get();
        if (!task)
            break;

        auto downloader = factory->create(task->uri, task->fname);
        if (!downloader)
            continue;

        job_list.emplace_back(task, downloader);
    }

    loop->run();

    return 0;
}
