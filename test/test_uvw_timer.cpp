#include <gtest/gtest.h>

#include <uvw/timer.hpp>
#include <chrono>

using ::std::cout;
using ::std::endl;
using ::std::chrono::duration_cast;
using namespace ::std::chrono_literals;

struct Time
{
    using Clock = ::std::chrono::steady_clock;
    using Duration = ::std::chrono::milliseconds;

    Duration elapsed() noexcept
    {
        auto now = Clock::now();
        return duration_cast<Duration>(now - start);
    }

    Clock::time_point start = Clock::now();
};

TEST(aio_uvw__Timer, simple)
{
    auto loop = ::uvw::Loop::getDefault();
    auto timer = loop->resource<::uvw::TimerHandle>();

    Time time{};
    timer->once<::uvw::TimerEvent>( [&time](const auto&, const auto&) { cout << time.elapsed().count() << "ms Timer emit" << endl; } );
    timer->once<::uvw::ErrorEvent>( [](const auto& err, const auto&) { FAIL() << err.name() << " " << err.what(); } );

    cout << time.elapsed().count() << "Timer start" << endl;
    timer->start(25ms, 0ms);

    loop->run();
    ASSERT_TRUE(true);
}

TEST(aio_uvw__Timer, again)
{
    auto loop = ::uvw::Loop::getDefault();
    auto timer_1 = loop->resource<::uvw::TimerHandle>();
    auto timer_2 = loop->resource<::uvw::TimerHandle>();

    Time time{};
    timer_1->once<::uvw::TimerEvent>( [&time, &timer_2](const auto&, const auto&)
    {
        cout << time.elapsed().count() << "ms First timer emit, invoke again() for second timer" << endl;
        //timer_2->again();
        timer_2->stop();
        timer_2->start(50ms, 0ms);
    } );
    timer_1->once<::uvw::ErrorEvent>( [](const auto& err, const auto&) { FAIL() << err.name() << " " << err.what(); } );

    timer_2->once<::uvw::TimerEvent>( [&time](const auto&, const auto&) { cout << time.elapsed().count() << "ms Second timer emit" << endl; } );
    timer_2->once<::uvw::ErrorEvent>( [](const auto& err, const auto&) { FAIL() << err.name() << " " << err.what(); } );

    cout << time.elapsed().count() << "Timers start" << endl;
    timer_1->start(25ms, 0ms);
    timer_2->start(50ms, 0ms);

    loop->run();
    cout << time.elapsed().count() << "Test done" << endl;
    ASSERT_TRUE(true);
}

