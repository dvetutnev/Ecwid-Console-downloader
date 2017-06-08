#pragma once

#include "aio_uvw_tcp.h"
#include "bandwidth.h"
#include "data_chunk.h"

#include <cassert>
#include <numeric>
#include <queue>

namespace uvw {

class TCPSocketBandwidth final : public TCPSocket, public bandwidth::Stream, public std::enable_shared_from_this<TCPSocketBandwidth>
{
    using Controller = ::bandwidth::Controller;

public:
    TCPSocketBandwidth(ConstructorAccess, std::shared_ptr<Controller> c, std::shared_ptr<TCPSocket>&& s) noexcept
        : controller{ std::move(c) },
          socket{ std::move(s) }
    {}
    static std::shared_ptr<TCPSocketBandwidth> create(std::shared_ptr<void>, std::shared_ptr<Controller>, std::shared_ptr<TCPSocket>);

    virtual void connect(const std::string& ip, unsigned short port) override { socket->connect(ip, port); }
    virtual void connect6(const std::string& ip, unsigned short port) override { socket->connect6(ip, port); }
    virtual void read() override;
    virtual void stop() noexcept override;
    virtual void write(std::unique_ptr<char[]> data, std::size_t length) override
    {
        assert(length <= std::numeric_limits<unsigned int>::max());
        socket->write(std::move(data), length);
    }
    virtual void shutdown() override { socket->shutdown(); }
    virtual bool active() const noexcept override;
    virtual void close() noexcept override;

    virtual void set_buffer(std::size_t length) noexcept override { buffer_max_length = length; }
    virtual std::size_t available() const noexcept override { return buffer_used; }
    virtual void transfer(std::size_t) override;

    TCPSocketBandwidth() = delete;
    TCPSocketBandwidth(const TCPSocketBandwidth&) = delete;
    TCPSocketBandwidth& operator= (const TCPSocketBandwidth&) = delete;
    virtual ~TCPSocketBandwidth() = default;

private:
    std::shared_ptr<Controller> controller;
    std::shared_ptr<TCPSocket> socket;

    bool closed = false;

    template < typename Event >
    void on_event(Event& event)
    {
        publish( std::move(event) );
        if (!closed)
            socket->template once<Event>( bind_on_event<Event>( shared_from_this()) );
    }

    template < typename Event >
    static std::function< void(Event&, const TCPSocket&) > bind_on_event(std::shared_ptr<TCPSocketBandwidth> self)
    {
        return [self = std::move(self)](Event& event, const TCPSocket&) { self->on_event<Event>(event); };
    }


    void on_data(std::unique_ptr<char[]>, std::size_t);
    static std::function< void(uvw::DataEvent&, const TCPSocket&) > bind_on_data(std::shared_ptr<TCPSocketBandwidth>);
    std::unique_ptr<char[]> pop_buffer(std::size_t);

    std::size_t buffer_used = 0;
    std::size_t buffer_max_length = 0;
    bool paused = false;
    bool stopped = true;
    bool eof = false;
    std::queue<DataChunk> buffer;
};

} // namespace uvw
