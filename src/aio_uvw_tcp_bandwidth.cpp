#include "aio_uvw_tcp_bandwidth.h"

#include <algorithm>

using std::size_t;
using std::shared_ptr;
using std::make_shared;
using std::unique_ptr;
using std::make_unique;
using std::move;
using std::function;
using std::min;
using std::copy_n;

using uvw::TCPSocket;
using uvw::TCPSocketBandwidth;
using uvw::ErrorEvent;
using uvw::ConnectEvent;
using uvw::WriteEvent;
using uvw::DataEvent;
using uvw::EndEvent;
using uvw::ShutdownEvent;

shared_ptr<TCPSocketBandwidth> TCPSocketBandwidth::create(shared_ptr<void>, shared_ptr<Controller> controller, shared_ptr<TCPSocket> socket)
{
    auto self = make_shared<TCPSocketBandwidth>( ConstructorAccess{42}, controller, move(socket) );
    controller->add_stream(self);

    self->socket->once<ErrorEvent>( bind_on_event<ErrorEvent>(self) );
    self->socket->once<ConnectEvent>( bind_on_event<ConnectEvent>(self) );
    self->socket->once<WriteEvent>( bind_on_event<WriteEvent>(self) );
    self->socket->once<ShutdownEvent>( bind_on_event<ShutdownEvent>(self) );

    self->socket->once<DataEvent>( bind_on_data(self) );
    self->socket->once<EndEvent>( [self](const auto&, const auto&) { self->eof = true; } );

    return self;
}

void TCPSocketBandwidth::read()
{
    stopped = false;
    if (!paused)
        socket->read();
}

void TCPSocketBandwidth::stop() noexcept
{
    stopped = true;
    if (!paused)
        socket->stop();
}

bool TCPSocketBandwidth::active() const noexcept
{
    return !stopped || socket->active();
}

void TCPSocketBandwidth::close() noexcept
{
    if (!closed)
    {
        closed = stopped = true;
        controller->remove_stream( shared_from_this() );
        socket->clear();
        socket->once<CloseEvent>( [self = shared_from_this()](auto& event, const auto&) { self->publish( move(event) ); } );
        socket->close();
    }
}

void TCPSocketBandwidth::transfer(size_t length)
{
    if (stopped || length == 0)
        return;

    length = min(length, buffer_used);
    auto data = pop_buffer(length);
    buffer_used -= length;
    publish( DataEvent{move(data), length} );

    if (eof && buffer_used == 0)
    {
        stopped = true;
        publish( EndEvent{} );
    } else if (paused && buffer_used < buffer_max_length)
    {
        paused = false;
        socket->read();
    }
}

unique_ptr<char[]> TCPSocketBandwidth::pop_buffer(size_t length)
{
    auto data = make_unique<char[]>(length);
    size_t offset = 0;

    do {
        DataChunk& chunk = buffer.front();
        size_t chunk_available = chunk.length - chunk.offset;
        char* chunk_ptr = chunk.data.get() + chunk.offset;

        if (length < chunk_available)
        {
            copy_n( chunk_ptr, length, data.get() + offset );
            chunk.offset += length;
            length = 0;
        } else
        {
            copy_n( chunk_ptr, chunk_available, data.get() + offset );
            buffer.pop();
            offset += chunk_available;
            length -= chunk_available;
        }

    } while (length > 0);

    return data;
}

void TCPSocketBandwidth::on_data(unique_ptr<char[]> data, size_t length)
{
    buffer.emplace(move(data), length);
    buffer_used += length;
    if (buffer_used >= buffer_max_length)
    {
        paused = true;
        socket->stop();
    }

    controller->shedule_transfer();

    if (!closed)
        socket->once<DataEvent>( bind_on_data(shared_from_this()) );
}

function< void(DataEvent&, const TCPSocket&) > TCPSocketBandwidth::bind_on_data(shared_ptr<TCPSocketBandwidth> self)
{
    return [self = move(self)] (DataEvent& event, const auto&) { self->on_data(move(event.data), event.length); };
}
