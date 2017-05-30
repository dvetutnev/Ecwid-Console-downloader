#include "task_simple.h"
#include "factory_simple.h"
#include "on_tick_simple.h"

#include <fstream>
#include <iostream>

using namespace std;

int main(int argc, char *argv[])
{
    const string task_fname = "task_list.txt";
    const size_t concurrency = 2;

    ifstream task_stream{task_fname};
    if ( !task_stream.is_open() )
    {
        cout << "Can`t open <" << task_fname << ">, break." << endl;
        return 1;
    }
    TaskListSimple task_list{task_stream, "./"};
    JobList job_list;

    auto loop = AIO_UVW::Loop::getDefault();
    auto factory = make_shared<FactorySimple>(loop);
    auto on_tick = make_shared< OnTickSimple<JobList> >(job_list, factory, task_list);
    factory->set_OnTick(on_tick);

    for (size_t i = 1; i <= concurrency; i++)
    {
        auto task = task_list.get();
        if (!task)
            break;

        auto downloader = factory->create(*task);
        if (!downloader)
            continue;

        job_list.emplace_back(task, downloader);
    }

    loop->run();

    return 0;
}
