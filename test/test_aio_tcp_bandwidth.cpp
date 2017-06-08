#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "aio_tcp_socket_mock.h"
#include "bandwidth_controller_mock.h"
#include "aio_uvw.h"

#include "aio_uvw_tcp_bandwidth.h"

using namespace std;
using namespace bandwidth;

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Invoke;
using ::testing::DoAll;
using ::testing::Mock;
using ::testing::InSequence;

struct TCPSocketBandwidthF : public ::testing::Test
{
    TCPSocketBandwidthF()
        : loop{ ::uvw::Loop::getDefault() },
          controller{ make_shared<ControllerMock>() },
          socket{ make_shared<TCPSocketMock>() },
          buffer_length{ 4 * 1024 }
    {
        weak_ptr<Stream> stream;
        EXPECT_CALL( *controller, add_stream(_) )
                .WillOnce( DoAll( SaveArg<0>(&stream),
                                  Invoke( [this](weak_ptr<Stream> w) { auto s = w.lock(); ASSERT_TRUE(s); s->set_buffer(buffer_length); } )
                                  ) );

        resource = loop->resource<uvw::TCPSocketBandwidth>(controller, socket);
        EXPECT_TRUE(resource);

        Mock::VerifyAndClearExpectations(controller.get());
        EXPECT_EQ(stream.lock(), resource);
    }

    void resource_close()
    {
        resource->clear();
        resource->once<uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
        bool cb_called = false;
        resource->once<uvw::CloseEvent>( [&cb_called, this](const auto&, auto& handle)
        {
            cb_called = true;
            auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);
        } );
        resource->once<uvw::ErrorEvent>( [](const auto&, const auto&) { FAIL(); } );

        weak_ptr<Stream> stream;
        EXPECT_CALL( *controller, remove_stream(_) )
                .WillOnce( SaveArg<0>(&stream) );
        EXPECT_CALL( *socket, close_() )
                .Times(1);

        resource->close();
        socket->publish( uvw::CloseEvent{} );

        Mock::VerifyAndClearExpectations(controller.get());
        Mock::VerifyAndClearExpectations(socket.get());
        ASSERT_EQ(stream.lock(), resource);
        ASSERT_TRUE(cb_called);
    }

    virtual ~TCPSocketBandwidthF()
    {
        EXPECT_LE(loop.use_count(), 2);
        EXPECT_LE(controller.use_count(), 2);
        EXPECT_LE(socket.use_count(), 2);

        EXPECT_TRUE( resource.unique() );
    }

    shared_ptr<::uvw::Loop> loop;
    shared_ptr<ControllerMock> controller;
    shared_ptr<TCPSocketMock> socket;

    shared_ptr<::uvw::TCPSocketBandwidth> resource;

    const size_t buffer_length;
};

TEST_F(TCPSocketBandwidthF, create_and_close)
{
    resource_close();
}

TEST_F(TCPSocketBandwidthF, connect_failed)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);
    resource->once<uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::CloseEvent>( [](const auto&, const auto&) { FAIL(); } );
    bool cb_called = false;
    uvw::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<uvw::ErrorEvent>( [&cb_called, &event, this](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );

    resource->connect("127.0.0.1", 8080);
    socket->publish(event);

    ASSERT_TRUE(cb_called);
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidthF, connect_and_shutdown)
{
    const string ip = "127.0.0.1";
    const unsigned short port = 8080;
    EXPECT_CALL( *socket, connect(ip, port) )
            .Times(1);
    bool cb_connect_called = false;
    resource->once<uvw::ConnectEvent>( [&cb_connect_called, this](const auto&, auto& handle)
    {
        cb_connect_called = true;
        auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::CloseEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ErrorEvent>( [](const auto&, const auto&) { FAIL(); } );

    resource->connect(ip, port);
    socket->publish( uvw::ConnectEvent{} );
    Mock::VerifyAndClearExpectations(socket.get());
    ASSERT_TRUE(cb_connect_called);

    EXPECT_CALL( *socket, shutdown() )
            .Times(1);
    resource->clear<uvw::ShutdownEvent>();
    bool cb_shutdown_called = false;
    resource->once<uvw::ShutdownEvent>( [&cb_shutdown_called, this](const auto&, auto& handle)
    {
        cb_shutdown_called = true;
        auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );

    resource->shutdown();
    socket->publish( uvw::ShutdownEvent{} );
    Mock::VerifyAndClearExpectations(socket.get());
    ASSERT_TRUE(cb_shutdown_called);

    resource_close();
}

TEST_F(TCPSocketBandwidthF, connect6_and_close)
{
    const string ip = "::1";
    const unsigned short port = 8080;
    EXPECT_CALL( *socket, connect6(ip, port) )
            .Times(1);
    bool cb_connect_called = false;
    resource->once<uvw::ConnectEvent>( [&cb_connect_called, this](const auto&, auto& handle)
    {
        cb_connect_called = true;
        auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::CloseEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ErrorEvent>( [](const auto&, const auto&) { FAIL(); } );

    resource->connect6(ip, port);
    socket->publish( uvw::ConnectEvent{} );
    Mock::VerifyAndClearExpectations(socket.get());
    ASSERT_TRUE(cb_connect_called);

    resource_close();
}

TEST_F(TCPSocketBandwidthF, write_failed_and_close)
{
    resource->once<uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::CloseEvent>( [](const auto&, const auto&) { FAIL(); } );
    bool cb_called = false;
    uvw::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<uvw::ErrorEvent>( [&cb_called, &event, this](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);

        resource->clear<uvw::CloseEvent>();
        resource->close();
    } );

    const size_t length = 42;
    char* const raw_ptr = new char[length];
    {
        InSequence s;
        EXPECT_CALL( *socket, write_(raw_ptr, length) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }

    resource->write(unique_ptr<char[]>{raw_ptr}, length);

    EXPECT_CALL( *controller, remove_stream(_) )
            .Times(1);

    socket->publish(event);
    Mock::VerifyAndClearExpectations(socket.get());
    ASSERT_TRUE(cb_called);

    socket->publish( uvw::CloseEvent{} );
    Mock::VerifyAndClearExpectations(controller.get());
    Mock::VerifyAndClearExpectations(socket.get());
}

TEST_F(TCPSocketBandwidthF, write_normal)
{
    resource->once<uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    bool cb_called = false;
    resource->once<uvw::WriteEvent>( [&cb_called, this](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast<uvw::TCPSocketBandwidth*>(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::CloseEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<uvw::ErrorEvent>( [](const auto&, const auto&) { FAIL(); } );

    const size_t length = 42;
    char* const raw_ptr = new char[length];
    EXPECT_CALL( *socket, write_(raw_ptr, length) )
            .Times(1);
    EXPECT_CALL( *socket, active_() )
            .WillOnce( Return(true) )
            .WillOnce( Return(false) );

    resource->write(unique_ptr<char[]>{raw_ptr}, length);
    EXPECT_TRUE(resource->active());

    socket->publish( uvw::WriteEvent{} );

    EXPECT_TRUE(cb_called);
    EXPECT_FALSE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

struct TCPSocketBandwidth_read : public TCPSocketBandwidthF
{
    TCPSocketBandwidth_read()
        : segment_count{5},
          segment_length{1000},
          segments{ generate_segments(segment_count, segment_length) }
    {
        resource->once<uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::ShutdownEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::CloseEvent>( [](const auto&, const auto&) { FAIL(); } );
        resource->once<uvw::ErrorEvent>( [](const auto&, const auto&) { FAIL(); } );
        EXPECT_CALL( *socket, read() )
                .Times(1);
        resource->read();
        Mock::VerifyAndClearExpectations(socket.get());
    }

    using Segment = std::pair< unique_ptr<char[]>, size_t >;
    static std::vector<Segment> generate_segments(size_t count, size_t length)
    {
        auto generate_chunk = [length]()
        {
            char* ptr = new char[length];
            std::random_device rd{};
            std::uniform_int_distribution<unsigned int> dist{0, 255};
            std::generate_n( ptr, length, [&dist, &rd]() { return static_cast<char>( dist(rd) ); } );
            return Segment{unique_ptr<char[]>{ptr}, length};
        };
        std::vector<Segment> chunks{count};
        std::generate(std::begin(chunks), std::end(chunks), generate_chunk);
        return chunks;
    }

    const size_t segment_count;
    const size_t segment_length;

    std::vector<Segment> segments;
};

TEST_F(TCPSocketBandwidth_read, pause)
{
    // filling buffer
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times(4);
    EXPECT_CALL( *socket, stop() )
            .Times(0);
    socket->publish( uvw::DataEvent{move(segments[0].first), segments[0].second} );
    socket->publish( uvw::DataEvent{move(segments[1].first), segments[1].second} );
    socket->publish( uvw::DataEvent{move(segments[2].first), segments[2].second} );
    socket->publish( uvw::DataEvent{move(segments[3].first), segments[3].second} );
    Mock::VerifyAndClearExpectations(controller.get());
    Mock::VerifyAndClearExpectations(socket.get());
    // read pause
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times(1);
    EXPECT_CALL( *socket, stop() )
            .Times(1);
    socket->publish( uvw::DataEvent{move(segments[4].first), segments[4].second} );
    EXPECT_TRUE(resource->active());
    EXPECT_EQ(resource->available(), 5000);
    Mock::VerifyAndClearExpectations(controller.get());
    Mock::VerifyAndClearExpectations(socket.get());
    // transfer
    bool cb_called = false;
    resource->clear<uvw::DataEvent>();
    resource->once<uvw::DataEvent>( [&cb_called](const uvw::DataEvent& event, const auto&) { cb_called = true; EXPECT_EQ(event.length, 500); } );
    EXPECT_CALL( *socket, read() )
            .Times(0);
    resource->transfer(500);
    EXPECT_TRUE(cb_called);
    EXPECT_EQ(resource->available(), 4500);
    EXPECT_TRUE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());
    // continue read
    cb_called = false;
    resource->once<uvw::DataEvent>( [&cb_called](const uvw::DataEvent& event, const auto&) { cb_called = true; EXPECT_EQ(event.length, 500); } );
    EXPECT_CALL( *socket, read() )
            .Times(1);
    resource->transfer(500);
    EXPECT_TRUE(cb_called);
    EXPECT_EQ(resource->available(), 4000);
    EXPECT_TRUE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());
    // dont repet read, transfer only available length
    cb_called = false;
    resource->once<uvw::DataEvent>( [&cb_called](const uvw::DataEvent& event, const auto&) { cb_called = true; EXPECT_EQ(event.length, 4000); } );
    EXPECT_CALL( *socket, read() )
            .Times(0);
    resource->transfer(4042);
    EXPECT_TRUE(cb_called);
    EXPECT_EQ(resource->available(), 0);
    EXPECT_TRUE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidth_read, ignore_trasfer_if_stopped)
{
    // transfer data, but not stopped
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times(1);
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(true) );
    socket->publish( uvw::DataEvent{move(segments[0].first), segments[0].second} );
    bool cb_called = false;
    resource->clear<uvw::DataEvent>();
    resource->once<uvw::DataEvent>( [&cb_called](const auto&, const auto&) { cb_called = true; } );
    resource->transfer(500);
    EXPECT_TRUE(cb_called);
    EXPECT_EQ(resource->available(), 500);
    EXPECT_TRUE(resource->active());
    Mock::VerifyAndClearExpectations(controller.get());
    // ignore transfer if stoped
    EXPECT_CALL( *socket, stop() )
            .Times(1);
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(false) );
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->stop();
    resource->transfer(500);
    EXPECT_EQ(resource->available(), 500);
    EXPECT_FALSE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidth_read, ignore_stop_read_if_paused)
{
    // filling buffer
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times( static_cast<int>(segment_count) );
    EXPECT_CALL( *socket, stop() )
            .Times(1);
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(false) );
    for (auto& item : segments)
        socket->publish( uvw::DataEvent{move(item.first), item.second} );
    EXPECT_TRUE(resource->active());
    Mock::VerifyAndClearExpectations(controller.get());
    Mock::VerifyAndClearExpectations(socket.get());
    // check stop
    EXPECT_CALL( *socket, stop() )
            .Times(0);
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(false) );
    resource->clear<uvw::DataEvent>();
    resource->once<uvw::DataEvent>( [this](const auto&, const auto&) { resource->stop(); } );
    resource->transfer(500);
    EXPECT_FALSE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());
    // check read
    EXPECT_CALL( *socket, read() )
            .Times(0);
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(false) );
    resource->read();
    EXPECT_TRUE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidth_read, trasfer_EOF)
{
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times(1);
    socket->publish( uvw::DataEvent{move(segments[0].first), segments[0].second} );
    socket->publish( uvw::EndEvent{} );

    resource->clear<uvw::DataEvent>();
    resource->transfer(segment_length / 2);

    resource->clear<uvw::EndEvent>();
    bool cb_data_called = false;
    bool cb_eof_called = false;
    bool eof_after_data = false;
    resource->once<uvw::DataEvent>( [&cb_data_called](const auto&, const auto&) { cb_data_called = true; } );
    resource->once<uvw::EndEvent>( [&cb_data_called, &cb_eof_called, &eof_after_data](const auto&, const auto&)
    {
        cb_eof_called = true;
        if (cb_data_called)
            eof_after_data = true;
    } );

    resource->transfer(segment_length / 2);

    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(false) );

    EXPECT_TRUE(cb_data_called);
    EXPECT_TRUE(cb_eof_called);
    EXPECT_TRUE(eof_after_data);
    EXPECT_EQ(resource->available(), 0);
    EXPECT_FALSE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidth_read, paused_EOF)
{
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times( static_cast<int>(segment_count) );
    EXPECT_CALL( *socket, stop() )
            .Times(1);
    EXPECT_CALL( *socket, read() )
            .Times(0);
    for (auto& item : segments)
        socket->publish( uvw::DataEvent{move(item.first), item.second} );
    socket->publish( uvw::EndEvent{} );

    resource->clear<uvw::DataEvent>();
    resource->clear<uvw::EndEvent>();
    bool cb_data_called = false;
    bool cb_eof_called = false;
    bool eof_after_data = false;
    resource->once<uvw::DataEvent>( [&cb_data_called](const auto&, const auto&) { cb_data_called = true; } );
    resource->once<uvw::EndEvent>( [&cb_data_called, &cb_eof_called, &eof_after_data](const auto&, const auto&)
    {
        cb_eof_called = true;
        if (cb_data_called)
            eof_after_data = true;
    } );

    resource->transfer(segment_count * segment_length);

    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( Return(false) );

    EXPECT_TRUE(cb_data_called);
    EXPECT_TRUE(cb_eof_called);
    EXPECT_TRUE(eof_after_data);
    EXPECT_EQ(resource->available(), 0);
    EXPECT_FALSE(resource->active());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidth_read, transfer_normal)
{
    string buff;
    for (auto& item : segments)
        buff.append(item.first.get(), item.second);

    EXPECT_CALL( *controller, shedule_transfer() )
            .Times( static_cast<int>(segment_count) );
    EXPECT_CALL( *socket, stop() )
            .Times(1);
    EXPECT_CALL( *socket, read() )
            .Times(1);
    for (auto& item : segments)
        socket->publish( uvw::DataEvent{move(item.first), item.second} );

    string buffer;
    std::function< void(uvw::DataEvent&, const uvw::TCPSocket&) > handler;
    handler = [&buffer, &handler, this](uvw::DataEvent& event, const uvw::TCPSocket&)
    {
        buffer.append(event.data.get(), event.length);
        resource->once<uvw::DataEvent>(handler);
    };
    resource->clear<uvw::DataEvent>();
    resource->once<uvw::DataEvent>(handler);

    resource->transfer(1000);
    resource->transfer(500);
    resource->transfer(500);
    resource->transfer(1500);
    resource->transfer(1500);

    EXPECT_EQ(buffer.size(), buff.size());
    EXPECT_EQ(buffer, buff);

    Mock::VerifyAndClearExpectations(controller.get());
    Mock::VerifyAndClearExpectations(socket.get());

    resource_close();
}

TEST_F(TCPSocketBandwidth_read, transfer_zero)
{
    EXPECT_CALL( *controller, shedule_transfer() )
            .Times(1);
    socket->publish( uvw::DataEvent{move(segments[0].first), segments[0].second} );

    resource->clear<uvw::DataEvent>();
    resource->once<uvw::DataEvent>( [](const auto&, const auto&) { FAIL() << "Publish DataEvent if transfer zero!";} );

    resource->transfer(0);

    Mock::VerifyAndClearExpectations(controller.get());

    resource_close();
}
