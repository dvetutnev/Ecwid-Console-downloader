#pragma once

#include "aio_uvw_tcp.h"
#include "bandwidth.h"
#include "data_chunk.h"

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

    virtual void connect(const std::string&, unsigned short) override;
    virtual void connect6(const std::string&, unsigned short) override;
    virtual void read() override;
    virtual void stop() noexcept override;
    virtual void write(std::unique_ptr<char[]>, std::size_t) override;
    virtual void shutdown() override;
    virtual bool active() const noexcept override;
    virtual void close() noexcept override;

    virtual void set_buffer(std::size_t) noexcept override;
    virtual std::size_t available() const noexcept override;
    virtual void transfer(std::size_t) override;

    TCPSocketBandwidth() = delete;
    TCPSocketBandwidth(const TCPSocketBandwidth&) = delete;
    TCPSocketBandwidth(TCPSocketBandwidth&&) = delete;
    TCPSocketBandwidth& operator= (const TCPSocketBandwidth&) = delete;
    TCPSocketBandwidth& operator= (TCPSocketBandwidth&&) = delete;
    virtual ~TCPSocketBandwidth() = default;

private:
    std::shared_ptr<Controller> controller;
    std::shared_ptr<TCPSocket> socket;

    Controller::StreamConnection conn;

    bool closed = false;

    template < typename Event >
    void on_event(Event&);
    template < typename Event >
    static std::function< void(Event&, const TCPSocket&) > bind_on_event(std::shared_ptr<TCPSocketBandwidth>);

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
