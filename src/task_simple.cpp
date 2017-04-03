#include "task_simple.h"
#include <sstream>

std::shared_ptr<Task> TaskListSimple::get()
{
    using namespace std;
    shared_ptr<Task> ret;

    while( !stream.eof() )
    {
        string buf;
        getline(stream, buf);
        if ( buf.empty() )
            continue;

        istringstream sbuf{ move(buf) };
        string uri, fname;
        sbuf >> uri;
        sbuf >> fname;
        if ( uri.empty() || fname.empty() )
            continue;

        ret = make_shared<Task>( move(uri), path + fname );
        break;
    }

  return ret;
}
