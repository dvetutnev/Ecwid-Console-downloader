#pragma once

#include <gmock/gmock.h>
#include <uvw/timer.hpp>

struct TimerHandleMock : public uvw::Emitter<TimerHandleMock>
{
    using Time = uvw::TimerHandle::Time;

    MOCK_METHOD2( start, void(Time, Time) );
    void close() noexcept { close_(); }
    MOCK_METHOD0( close_, void() );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<TimerHandleMock>::publish( std::forward<Event>(event) ); }
};
