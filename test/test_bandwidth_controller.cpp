#include <gtest/gtest.h>

#include "bandwidth_controller.h"

#include "bandwidth_stream_mock.h"
#include "bandwidth_time_mock.h"

using ::bandwidth::Stream;
using ::bandwidth::StreamMock;
using ::bandwidth::TimeMock;
using ::bandwidth::ControllerSimple;

using ::std::size_t;
using ::std::shared_ptr;
using ::std::make_shared;
using ::std::unique_ptr;
using ::std::make_unique;
using ::std::move;
using ::std::chrono::milliseconds;

using ::testing::_;
using ::testing::Return;
using ::testing::Mock;
using ::testing::ReturnPointee;
using ::testing::Invoke;

struct bandwidth_ControllerSimpleF : public ::testing::Test
{
    bandwidth_ControllerSimpleF()
        : limit{9000},
          buffer_reserve{ limit * 4 }
    {
        auto t = make_unique<TimeMock>();
        time = t.get();
        controller = make_shared<ControllerSimple>( limit, move(t) );
    }

    const size_t limit;
    const size_t buffer_reserve;

    TimeMock* time;
    shared_ptr<ControllerSimple> controller;
};

TEST_F(bandwidth_ControllerSimpleF, set_buffer_in_stream)
{
    auto stream = make_shared<StreamMock>();
    EXPECT_CALL( *stream, set_buffer_(buffer_reserve) )
            .Times(1);

    controller->add_stream(stream);

    Mock::VerifyAndClearExpectations( stream.get() );
    ASSERT_TRUE( stream.unique() );
}

TEST_F(bandwidth_ControllerSimpleF, ignore_nullptr)
{
    shared_ptr<Stream> null_ptr{};
    ASSERT_NO_THROW( controller->add_stream(null_ptr) );

    EXPECT_CALL( *time, elapsed_() )
            .WillRepeatedly( Return( milliseconds{1000} ) );
    ASSERT_NO_THROW( controller->shedule_transfer() );
}


struct bandwidth_ControllerSimpleStreams : public bandwidth_ControllerSimpleF
{
    bandwidth_ControllerSimpleStreams()
        : stream_1{ make_shared<StreamMock>() },
          stream_2{ make_shared<StreamMock>() },
          stream_3{ make_shared<StreamMock>() }
    {
        EXPECT_CALL( *stream_1, set_buffer_(buffer_reserve) )
                .Times(1);
        EXPECT_CALL( *stream_2, set_buffer_(buffer_reserve) )
                .Times(1);
        EXPECT_CALL( *stream_3, set_buffer_(buffer_reserve) )
                .Times(1);

        controller->add_stream(stream_1);
        controller->add_stream(stream_2);
        controller->add_stream(stream_3);

        Mock::VerifyAndClearExpectations( stream_1.get() );
        Mock::VerifyAndClearExpectations( stream_2.get() );
        Mock::VerifyAndClearExpectations( stream_3.get() );
    }

    void check_streams()
    {
        Mock::VerifyAndClearExpectations( stream_1.get() );
        Mock::VerifyAndClearExpectations( stream_2.get() );
        Mock::VerifyAndClearExpectations( stream_3.get() );
    }

    virtual ~bandwidth_ControllerSimpleStreams()
    {
        EXPECT_TRUE( stream_1.unique() );
        EXPECT_TRUE( stream_2.unique() );
        EXPECT_TRUE( stream_3.unique() );
    }

    shared_ptr<StreamMock> stream_1;
    shared_ptr<StreamMock> stream_2;
    shared_ptr<StreamMock> stream_3;
};

TEST_F(bandwidth_ControllerSimpleStreams, simple_test)
{
    EXPECT_CALL( *time, elapsed_() )
            .WillOnce( Return( milliseconds{1000} ) );

    EXPECT_CALL( *stream_1, available_() )
            .WillOnce( Return( limit / 3 ) );
    EXPECT_CALL( *stream_1, transfer( limit / 3 ) )
            .Times(1);
    EXPECT_CALL( *stream_2, available_() )
            .WillOnce( Return( limit / 3 ) );
    EXPECT_CALL( *stream_2, transfer( limit / 3 ) )
            .Times(1);
    EXPECT_CALL( *stream_3, available_() )
            .WillOnce( Return( limit / 3 ) );
    EXPECT_CALL( *stream_3, transfer( limit / 3 ) )
            .Times(1);

    controller->shedule_transfer();

    Mock::VerifyAndClearExpectations( time );
    check_streams();
}

TEST_F(bandwidth_ControllerSimpleStreams, dont_transfer_empty)
{
    EXPECT_CALL( *time, elapsed_() )
            .WillOnce( Return( milliseconds{1000} ) );

    EXPECT_CALL( *stream_1, available_() )
            .WillOnce( Return(0) );
    EXPECT_CALL( *stream_1, transfer(_) )
            .Times(0);

    EXPECT_CALL( *stream_2, available_() )
            .WillOnce( Return( limit / 3 ) );
    EXPECT_CALL( *stream_2, transfer( limit / 3 ) )
            .Times(1);
    EXPECT_CALL( *stream_3, available_() )
            .WillOnce( Return( limit / 3 ) );
    EXPECT_CALL( *stream_3, transfer( limit / 3 ) )
            .Times(1);

    controller->shedule_transfer();

    Mock::VerifyAndClearExpectations( time );
    check_streams();
}

TEST_F(bandwidth_ControllerSimpleStreams, redistribution_unused_bandwidth)
{
    EXPECT_CALL( *time, elapsed_() )
            .WillOnce( Return( milliseconds{1000} ) );

    size_t available_1 = 3500;
    size_t count_1 = 0;
    EXPECT_CALL( *stream_1, available_() )
            .WillRepeatedly( ReturnPointee(&available_1) );
    EXPECT_CALL( *stream_1, transfer(_) )
            .WillRepeatedly( Invoke( [&available_1, &count_1](size_t size)
    {
        ASSERT_GE(available_1, size);
        available_1 -= size;
        count_1++;
    } ) );

    size_t available_2 = 1000;
    size_t count_2 = 0;
    EXPECT_CALL( *stream_2, available_() )
            .WillRepeatedly( ReturnPointee(&available_2) );
    EXPECT_CALL( *stream_2, transfer(_) )
            .WillRepeatedly( Invoke( [&available_2, &count_2](size_t size)
    {
        ASSERT_GE(available_2, size);
        available_2 -= size;
        count_2++;
    } ) );

    size_t available_3 = 4500;
    size_t count_3 = 0;
    EXPECT_CALL( *stream_3, available_() )
            .WillRepeatedly( ReturnPointee(&available_3) );
    EXPECT_CALL( *stream_3, transfer(_) )
            .WillRepeatedly( Invoke( [&available_3, &count_3](size_t size)
    {
        ASSERT_GE(available_3, size);
        available_3 -= size;
        count_3++;
    } ) );

    controller->shedule_transfer();

    EXPECT_EQ(available_1, 0);
    EXPECT_EQ(available_2, 0);
    EXPECT_EQ(available_3, 0);

    std::cout << "Count invoke transfer: stream_1 = " << count_1 << ", stream_2 = " << count_2 << ", stream_3 = " << count_3 << std::endl;
}
