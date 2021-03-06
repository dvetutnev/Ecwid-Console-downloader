#pragma once

#include <uvw/emitter.hpp>

namespace aio {

class TCPSocket : public ::uvw::Emitter<TCPSocket>
{
public:
    virtual void connect(const std::string& ip, unsigned short port) = 0;
    virtual void connect6(const std::string& ip, unsigned short port) = 0;
    virtual void read() = 0;
    virtual void stop() = 0;
    virtual void write(std::unique_ptr<char[]>, std::size_t) = 0;
    virtual void shutdown() = 0;
    virtual bool active() const noexcept= 0;
    virtual void close() noexcept = 0;
    virtual ~TCPSocket() = default;
protected:
    struct ConstructorAccess { explicit ConstructorAccess(int) {} };
};

} // namespace aio
