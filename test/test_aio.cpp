#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <functional>
#include <chrono>

#include "aio_uvpp.h"

using namespace std;

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Expectation;

template<typename aio = aio_uvpp>
class TimerClient : public std::enable_shared_from_this<TimerClient<aio>>
{
    using Loop = typename aio::Loop;
    using Timer = typename aio::Timer;

public:
    TimerClient(Loop& loop_, size_t max_count_ = 3)
        : loop{loop_},
          timer{loop.template create<Timer>()},
          max_count{max_count_},
          count{0}
    {}

    void run(std::function<void()> callback_)
    {
        count = 0;
        callback = move(callback_);
        timer->start( bind( &TimerClient::handler, this->template shared_from_this() ),
                      chrono::milliseconds{50},
                      chrono::milliseconds{50} );
    }

    ~TimerClient()
    {
        timer->close();
    }

private:
    Loop& loop;
    shared_ptr<Timer> timer;
    const size_t max_count;
    size_t count;
    function<void()> callback;

    void handler()
    {
        if ( ++count >= max_count )
            timer->stop();
        callback();
    }
};

class CallBackMock
{
public:
    MOCK_METHOD0( invoke, void() );
};

TEST(aio, aio_timer)
{
    aio_uvpp::Loop loop;
    const size_t count_call = 3;
    auto timer_client = make_shared<TimerClient<>>(loop, count_call);

    CallBackMock callback;
    EXPECT_CALL( callback, invoke() )
            .Times(count_call);

    timer_client->run( bind(&CallBackMock::invoke, &callback) );

    ASSERT_TRUE( loop.run() );
}

class TimerMock;
class LoopMock;
namespace LoopMock_detail {

template<typename T>
shared_ptr<T> create(LoopMock* self);

template<>
shared_ptr<TimerMock> create<TimerMock>(LoopMock* self);

}

class LoopMock
{
public:
    template<typename T>
    shared_ptr<T> create()
    {
        return LoopMock_detail::create<T>(this);
    }

    MOCK_METHOD0( createTimerMock, std::shared_ptr<TimerMock>() );
};

namespace LoopMock_detail {

template<typename T>
shared_ptr<T> create(LoopMock* self)
{
    return shared_ptr<T>{};
}

template<>
shared_ptr<TimerMock> create<TimerMock>(LoopMock* self)
{
    return self->createTimerMock();
}

}

class TimerMock
{
public:
    MOCK_METHOD3( start, void(function<void()>, chrono::milliseconds, chrono::milliseconds) );
    MOCK_METHOD0( stop, void() );
    MOCK_METHOD0( close, void() );
};

class aio_mock
{
public:
    using Loop = LoopMock;
    using Timer = TimerMock;
};

TEST(aio_mock, aio_timer)
{
    auto timer_ptr = make_shared<TimerMock>();
    LoopMock loop;
    Expectation timer_create = EXPECT_CALL( loop, createTimerMock() )
            .WillOnce( Return(timer_ptr) );

    function<void()> handler;
    Expectation timer_start = EXPECT_CALL( *timer_ptr, start(_, _, _) )
            .After(timer_create)
            .WillOnce( SaveArg<0>(&handler) );
    Expectation timer_stop = EXPECT_CALL( *timer_ptr, stop() )
            .Times(1)
            .After(timer_start);
    EXPECT_CALL( *timer_ptr, close() )
            .Times(1)
            .After(timer_stop);

    const size_t count_call = 4;
    auto timer_client = make_shared<TimerClient<aio_mock>>(loop, count_call);

    CallBackMock callback;
    EXPECT_CALL( callback, invoke() )
            .Times(count_call);

    timer_client->run( bind(&CallBackMock::invoke, &callback) );

    for ( size_t i = 1; i <= count_call; i++ )
        handler();
}

TEST(aio, resolver)
{
    aio_uvpp::Loop loop;
    aio_uvpp::Ressolver resolver{loop};
    const string ip = "127.0.0.1";
    const bool r = resolver.resolve(ip,
                     [&ip](const auto& e, bool ipv4, const auto& addr)
    {
        ASSERT_FALSE(e);
        ASSERT_EQ(addr, ip);
        ASSERT_TRUE(ipv4);
    } );
    ASSERT_TRUE(r);
    ASSERT_TRUE( loop.run() );
}
