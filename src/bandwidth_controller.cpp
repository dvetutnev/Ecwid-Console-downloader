#include "bandwidth_controller.h"

#include <cassert>

using ::std::size_t;
using ::std::move;
using ::std::begin;
using ::std::end;
using ::std::min;
using ::std::max;

using ::bandwidth::ControllerSimple;

void ControllerSimple::add_stream(std::weak_ptr<Stream> weak)
{
    auto stream = weak.lock();
    if (stream)
    {
        stream->set_buffer( limit * 4 );
        streams.push_back(weak);
    }
}

void ControllerSimple::remove_stream(std::weak_ptr<Stream>)
{

}


void ControllerSimple::shedule_transfer()
{
    const auto elapsed = time->elapsed().count();
    assert(elapsed >= 0);
    size_t total_to_transfer = ( limit * static_cast<size_t>(elapsed) ) / 1000;

    size_t pending_streams = streams.size();
    if (pending_streams == 0)
        return;

    do {
        size_t chunk = max( total_to_transfer / pending_streams, 1ul );
        pending_streams = 0;
        for (auto it = begin(streams); it != end(streams); ++it)
        {
            auto stream = it->lock();
            size_t available = stream->available();
            size_t to_transfer = min(available, chunk);
            if (to_transfer == 0)
                continue;

            stream->transfer(to_transfer);
            total_to_transfer -= to_transfer;
            if (available - to_transfer > 0)
                pending_streams++;
        }
    } while (total_to_transfer > 0 && pending_streams > 0);
}

