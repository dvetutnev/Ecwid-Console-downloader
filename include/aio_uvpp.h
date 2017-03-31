#pragma once

#include <memory>

#include <uvpp/error.hpp>
#include <uvpp/loop.hpp>
#include <uvpp/timer.hpp>
#include <uvpp/resolver.hpp>

namespace uvpp {
class LoopWithCreate : public loop
{
public:
    explicit LoopWithCreate(bool use_default = false)
        : loop(use_default)
    {}

    LoopWithCreate(const LoopWithCreate&) = delete;
    LoopWithCreate& operator= (const LoopWithCreate&) = delete;

    template< typename T, typename ...Args >
    std::unique_ptr<T> create(Args&&... args)
    {
        return std::make_unique<T>( *this, std::forward<Args>(args)... );
    }
};
}

class aio_uvpp
{
public:
    using Error = uvpp::error;
    using Loop = uvpp::LoopWithCreate;
    using Timer = uvpp::Timer;
    using Ressolver = uvpp::Resolver;
};
