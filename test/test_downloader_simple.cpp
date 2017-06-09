#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <regex>
#include <algorithm>
#include <random>

#include "downloader_simple.h"
#include "aio_loop_mock.h"
#include "aio_dns_mock.h"
#include "aio_tcp_socket_mock.h"
#include "aio_timer_mock.h"
#include "aio_file_mock.h"
#include "aio_uvw.h"
#include "on_tick_mock.h"
#include "http.h"

using namespace std;

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ByMove;
using ::testing::Mock;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::InSequence;
using ::testing::DoAll;
using ::testing::SaveArg;
using ::testing::AtLeast;
using ::testing::AnyNumber;

struct AIO_Mock
{
    using Loop = LoopMock;
    using ErrorEvent = AIO_UVW::ErrorEvent;

    using AddrInfoEvent = AIO_UVW::AddrInfoEvent;
    using GetAddrInfoReq = GetAddrInfoReqMock;
    using IPAddress = AIO_UVW::IPAddress;
    static auto addrinfo2IPAddress(const addrinfo* addr) { return AIO_UVW::addrinfo2IPAddress(addr); }

    using TCPSocket = uvw::TCPSocket;
    using TCPSocketSimple = TCPSocketMock;
    using ConnectEvent = AIO_UVW::ConnectEvent;
    using WriteEvent = AIO_UVW::WriteEvent;
    using DataEvent = AIO_UVW::DataEvent;
    using EndEvent = AIO_UVW::EndEvent;
    using ShutdownEvent = AIO_UVW::ShutdownEvent;

    using TimerHandle = TimerHandleMock;
    using TimerEvent = AIO_UVW::TimerEvent;

    using FileReq = FileReqMock;
    using FileOpenEvent = AIO_UVW::FileOpenEvent;
    using FileWriteEvent = AIO_UVW::FileWriteEvent;
    using FileCloseEvent = AIO_UVW::FileCloseEvent;

    using FsReq = FsReqMock;
};

/*------- HttpParserMock -------*/

using UriParseResult = HttpParser::UriParseResult;
struct HttpParserMock
{
    using UriParseResult = HttpParser::UriParseResult;
    static HttpParserMock* instance_uri_parse;
    static unique_ptr<UriParseResult> uri_parse(const string& uri) { return instance_uri_parse->uri_parse_(uri); }
    MOCK_METHOD1( uri_parse_, unique_ptr<UriParseResult>(const string&) );

    using OnData = HttpParser::OnData;
    using ResponseParseResult = HttpParser::ResponseParseResult;
    static HttpParserMock* instance_response_parse;
    static unique_ptr<HttpParserMock> create(OnData on_data) { return instance_response_parse->create_( std::move(on_data) ); }
    MOCK_METHOD1( create_, unique_ptr<HttpParserMock>(OnData) );
    const ResponseParseResult response_parse(unique_ptr<char[]> data, std::size_t len) { return response_parse_(data.get(), len); }
    MOCK_CONST_METHOD2( response_parse_, ResponseParseResult(const char[], std::size_t) );
};
HttpParserMock* HttpParserMock::instance_uri_parse;
HttpParserMock* HttpParserMock::instance_response_parse;

/*------- uri parse -------*/

struct DownloaderSimpleF : public ::testing::Test
{
    DownloaderSimpleF()
        : loop{ make_shared<LoopMock>() },
          on_tick{ make_shared<OnTickMock>() },
          instance_uri_parse{ make_unique<HttpParserMock>() },

          backlog{4},
          downloader{ make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick, backlog) }
    {
        HttpParserMock::instance_uri_parse = instance_uri_parse.get();
    }

    virtual ~DownloaderSimpleF()
    {
        EXPECT_TRUE( downloader.unique() );
        EXPECT_LE(loop.use_count(), 2);
        EXPECT_LE(on_tick.use_count(), 2);
    }

    shared_ptr<LoopMock> loop;
    shared_ptr<OnTickMock> on_tick;
    unique_ptr<HttpParserMock> instance_uri_parse;

    const std::size_t backlog;

    shared_ptr<Downloader> downloader;
};

TEST_F(DownloaderSimpleF, uri_parse_failed)
{
    const string bad_uri = "bad_uri";
    const Task task{bad_uri, ""};
    EXPECT_CALL( *instance_uri_parse, uri_parse_(bad_uri) )
            .WillOnce( Return( ByMove(nullptr) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    ASSERT_FALSE( downloader->run(task) );
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- host resolve -------*/

auto on_tick_handler = [](Downloader* d) { cout << "on_tick: status => " << d->status().state_str << endl; };

struct DownloaderSimpleResolve : public DownloaderSimpleF
{
    DownloaderSimpleResolve()
        : host{"www.internet.org"},
          port{8080},
          query{"/uri/fname.zip"},
          task{"www.internet.org", "fname"},
          resolver{ make_shared<GetAddrInfoReqMock>() }
    {
        auto uri_parsed = make_unique<UriParseResult>();
        uri_parsed->host = host;
        uri_parsed->port = port;
        uri_parsed->query = query;
        EXPECT_CALL( *instance_uri_parse, uri_parse_(task.uri) )
                .WillOnce( Return( ByMove( std::move(uri_parsed) ) ) );

        EXPECT_CALL( *loop, resource_GetAddrInfoReqMock() )
                .WillOnce( Return(resolver) );
    }

    virtual ~DownloaderSimpleResolve()
    {
        EXPECT_LE(resolver.use_count(), 2);
    }

    const string host;
    const unsigned short port;
    const string query;
    const Task task;

    shared_ptr<GetAddrInfoReqMock> resolver;
};

TEST_F(DownloaderSimpleResolve, dont_invoke_tick_if_resolver_failed_run)
{
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);
    EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
            .WillOnce( Invoke( [h = resolver.get()](string) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );

    ASSERT_FALSE( downloader->run(task) );

    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Failed);
    cout << "status.state_str => " << status.state_str << endl;
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleResolve_normalRun : public DownloaderSimpleResolve
{
    DownloaderSimpleResolve_normalRun()
    {
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);
        EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
                .Times(1);

        EXPECT_TRUE( downloader->run(task) );

        EXPECT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
        Mock::VerifyAndClearExpectations(instance_uri_parse.get());
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(resolver.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );
    }
};

TEST_F(DownloaderSimpleResolve_normalRun, host_resolve_failed)
{
    resolver->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EAI_NONAME) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleResolve_normalRun, stop)
{
    EXPECT_CALL( *resolver, cancel() )
            .WillOnce( Return(true) );

    downloader->stop();

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- connect -------*/

AIO_UVW::AddrInfoEvent create_addr_info_event(const string& ip)
{
    auto addrinfo_raw_ptr = new addrinfo;
    addrinfo_raw_ptr->ai_family = AF_INET;
    addrinfo_raw_ptr->ai_addrlen = sizeof(sockaddr_in);
    addrinfo_raw_ptr->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_in);
    uv_ip4_addr(ip.c_str(), 0, reinterpret_cast<sockaddr_in*>(addrinfo_raw_ptr->ai_addr));
    addrinfo_raw_ptr->ai_next = nullptr;
    addrinfo_raw_ptr->ai_canonname = nullptr;
    auto addrinfo_ptr = unique_ptr<addrinfo, void(*)(addrinfo*)>{addrinfo_raw_ptr, [](addrinfo* ptr) { uv_freeaddrinfo(ptr); } };
    return AIO_UVW::AddrInfoEvent{ std::move(addrinfo_ptr) };
}

AIO_UVW::AddrInfoEvent create_addr_info_event_ipv6(const string& ip)
{
    auto addrinfo_raw_ptr = new addrinfo;
    addrinfo_raw_ptr->ai_family = AF_INET6;
    addrinfo_raw_ptr->ai_addrlen = sizeof(sockaddr_in6);
    addrinfo_raw_ptr->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_in6);
    uv_ip6_addr(ip.c_str(), 0, reinterpret_cast<sockaddr_in6*>(addrinfo_raw_ptr->ai_addr));
    addrinfo_raw_ptr->ai_next = nullptr;
    addrinfo_raw_ptr->ai_canonname = nullptr;
    auto addrinfo_ptr = unique_ptr<addrinfo, void(*)(addrinfo*)>{addrinfo_raw_ptr, [](addrinfo* ptr) { uv_freeaddrinfo(ptr); } };
    return AIO_UVW::AddrInfoEvent{ std::move(addrinfo_ptr) };
}

struct DownloaderSimpleConnect_CreateHandles : public DownloaderSimpleResolve_normalRun {};

TEST_F(DownloaderSimpleConnect_CreateHandles, socket_create_failed)
{
    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .WillRepeatedly( Return(nullptr) );
    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .WillRepeatedly( Return(nullptr) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleConnect_CreateHandles, timer_create_failed)
{
    auto socket = make_shared<TCPSocketMock>();
    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .WillOnce( Return(socket) );
    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .WillOnce( Return(nullptr) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
}

struct DownloaderSimpleConnect : DownloaderSimpleConnect_CreateHandles
{
    DownloaderSimpleConnect()
        : socket{ make_shared<TCPSocketMock>() },
          timer{ make_shared<TimerHandleMock>() }
    {
        EXPECT_CALL( *loop, resource_TCPSocketMock() )
                .WillOnce( Return(socket) );
        EXPECT_CALL( *loop, resource_TimerHandleMock() )
                .WillOnce( Return(timer) );
    }

    virtual ~DownloaderSimpleConnect()
    {
        EXPECT_LE(socket.use_count(), 2);
        EXPECT_LE(timer.use_count(), 2);
    }

    shared_ptr<TCPSocketMock> socket;
    shared_ptr<TimerHandleMock> timer;
};

TEST_F(DownloaderSimpleConnect, connect_failed)
{
    const string ip = "127.0.0.1";
    EXPECT_CALL( *socket, connect(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);

    resolver->publish( create_addr_info_event(ip) );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, connect6_failed)
{
    const string ip = "::1";
    EXPECT_CALL( *socket, connect6(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);

    resolver->publish( create_addr_info_event_ipv6(ip) );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, connect_timeout)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);
    TimerHandleMock::Time repeat{0};
    TimerHandleMock::Time timeout{5000};
    EXPECT_CALL( *timer, start(timeout, repeat) )
            .Times(1);

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, timer_failed_on_run)
{
    {
        InSequence s;
        EXPECT_CALL( *socket, connect(_,_) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
    {
        InSequence s;
        EXPECT_CALL( *timer, start(_,_) )
                .WillOnce( Invoke( [h = timer.get()](const auto&, const auto&) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

/*------- http request write -------*/

struct DownloaderSimpleHttpRequest : public DownloaderSimpleConnect
{
    DownloaderSimpleHttpRequest()
        : ip{"127.0.0.1"},
          timeout{5000},
          repeat{0}
    {
        EXPECT_CALL( *socket, connect(ip, port) )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);

        resolver->publish( create_addr_info_event(ip) );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    void prepare_close_socket_and_timer()
    {
        EXPECT_CALL( *socket, close_() )
                .Times(1);
        EXPECT_CALL( *timer, close_() )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );
    }

    void check_close_socket_and_timer()
    {
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    const string ip;
    TimerHandleMock::Time timeout;
    TimerHandleMock::Time repeat;
};

TEST_F(DownloaderSimpleHttpRequest, write_failed__and_check_request_data)
{
    string request;
    EXPECT_CALL( *socket, write_(_,_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke( [&request](const char data[], unsigned int len) { request.append(data, len); } ) );
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(_,_) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    const string pattern_request_line = "^GET\\s" + query + "\\sHTTP/1.1\\r\\n";
    std::regex re_request_line{pattern_request_line};
    if ( !std::regex_search(request, re_request_line) )
        FAIL() << "Request failed, invalid request line. Request:" << endl << request << endl;

    const string pattern_host_header = "\\r\\nHost:\\s" + host + "\\r\\n";
    std::regex re_host_header{pattern_host_header};
    if ( !std::regex_search(request, re_host_header) )
        FAIL() << "Request failed, invalid Host header. Request:" << endl << request << endl;

    const string pattern_tail = "\\r\\n\\r\\n";
    std::regex re_tail{pattern_tail};
    if ( !std::regex_search(request, re_tail) )
        FAIL() << "Request failed, invalid tail. Request:" << endl << request << endl;

    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    prepare_close_socket_and_timer();
    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );
    check_close_socket_and_timer();
}

TEST_F(DownloaderSimpleHttpRequest, write_timeout)
{
    EXPECT_CALL( *socket, write_(_,_) )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    prepare_close_socket_and_timer();
    timer->publish( AIO_UVW::TimerEvent{} );
    check_close_socket_and_timer();
}

TEST_F(DownloaderSimpleHttpRequest, timer_failed_on_run)
{
    {
        InSequence s;
        EXPECT_CALL( *socket, write_(_,_) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .WillOnce( Invoke( [h = timer.get()](const auto&, const auto&) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );
    check_close_socket_and_timer();
}

/*------- read start -------*/

struct DownloaderSimpleReadStart : public DownloaderSimpleHttpRequest
{
    DownloaderSimpleReadStart()
    {
        EXPECT_CALL( *socket, write_(_,_) )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(timeout, repeat) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::ConnectEvent{} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *socket, read() )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(timeout, repeat) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::WriteEvent{} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }
};

TEST_F(DownloaderSimpleReadStart, unexpectedly_EOF)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::EndEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleReadStart, error_read)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleReadStart, timeout_read)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- response parse -------*/

struct DownloaderSimpleResponseParse : public DownloaderSimpleReadStart
{
    DownloaderSimpleResponseParse()
        : http_parser{ new HttpParserMock }
    {
        HttpParserMock::instance_response_parse = http_parser;
        EXPECT_CALL( *http_parser, create_(_) )
            .WillOnce( DoAll( SaveArg<0>(&handler_on_data),
                              Return( ByMove( unique_ptr<HttpParserMock>{http_parser} ) ) ) );
    }

    HttpParserMock* http_parser;
    HttpParser::OnData handler_on_data;
};

TEST_F(DownloaderSimpleResponseParse, redirect)
{
    const size_t len = 1510;
    char* data = new char[len];
    const string redirect_uri = "www.internet.org/redirect";
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Redirect;
    result.redirect_uri = redirect_uri;
    EXPECT_CALL( *http_parser, response_parse_(data, len) )
            .WillOnce( Return(result) );

    bool timer_stopped = false;
    EXPECT_CALL( *timer, stop() )
            .Times( AnyNumber() )
            .WillRepeatedly( Invoke( [&timer_stopped]() { timer_stopped = true; } ) );
    EXPECT_CALL( *timer, close_() )
            .Times(1);

    EXPECT_CALL( *socket, shutdown() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data}, len } );
    if (!timer_stopped)
        timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    EXPECT_CALL( *socket, close_() )
            .Times(1);

    socket->publish( AIO_UVW::ShutdownEvent{} );

    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Redirect);
    ASSERT_EQ(status.redirect_uri, redirect_uri);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleResponseParse, error_on_headers)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "404 Not found (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleFileOpen : public DownloaderSimpleResponseParse
{
    DownloaderSimpleFileOpen()
        : file{ make_shared<FileReqMock>() }
    {
        EXPECT_CALL( *loop, resource_FileReqMock() )
                .WillOnce( Return(file) );
    }

    virtual ~DownloaderSimpleFileOpen()
    {
        EXPECT_LE(file.use_count(), 2);
    }

    shared_ptr<FileReqMock> file;
};

TEST_F(DownloaderSimpleFileOpen, file_open_error)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    auto data = new char[len];
    auto on_data = [&data, &len, this]() { handler_on_data(unique_ptr<char[]>{data}, len); };
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
            .Times(1);
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleFileOpen, file_open_error_invoke)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    auto data = new char[len];
    auto on_data = [&data, &len, this]() { handler_on_data(unique_ptr<char[]>{data}, len); };
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
            .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } ); } ) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleFileWrite : public DownloaderSimpleFileOpen
{
    DownloaderSimpleFileWrite()
        : length_parser{42},
          data_parser{ new char[length_parser] },
          length_file{36},
          data_file{ new char[length_file] },
          fs{ make_shared<FsReqMock>() }
    {
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::InProgress;
        auto on_data = [this]() { handler_on_data(unique_ptr<char[]>{data_file}, length_file); };
        EXPECT_CALL( *http_parser, response_parse_(data_parser, length_parser) )
                .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                                  Return(result) ) );
        EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
                .Times(1);
        EXPECT_CALL( *timer, again() )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data_parser}, length_parser} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(http_parser);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *file, write(data_file, length_file, 0) )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);

        file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    virtual ~DownloaderSimpleFileWrite()
    {
        EXPECT_LE(fs.use_count(), 2);
    }

    const std::size_t length_parser;
    char* const data_parser;
    const std::size_t length_file;
    char* const data_file;

    std::shared_ptr<FsReqMock> fs;
};

TEST_F(DownloaderSimpleFileWrite, read_error)
{
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .Times(0);
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .WillOnce( Return(true) );
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, unexpected_EOF)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Connection lost, unexpected EOF (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(nullptr,_) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .WillOnce( Return(true) );
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::EndEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, parser_error)
{
    std::size_t len = 42;
    char* const data = new char[len];
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Response parse failed (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(data, len) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .WillOnce( Return(true) );
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data}, len} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, file_write_error)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    char* const data_parser = new char[len];
    char* const data_file = new char[len];
    auto on_data = [&data_file, &len, this]() { handler_on_data(unique_ptr<char[]>{data_file}, len); };
    EXPECT_CALL( *http_parser, response_parse_(data_parser, len) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data_parser}, len} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *file, write(data_file, len, length_file) )
            .Times(1);

    file->publish( AIO_UVW::FileWriteEvent{ task.fname.c_str(), length_file } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(file.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, file_write_error_invoke)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    char* const data_parser = new char[len];
    char* const data_file = new char[len];
    auto on_data = [&data_file, &len, this]() { handler_on_data(unique_ptr<char[]>{data_file}, len); };
    EXPECT_CALL( *http_parser, response_parse_(data_parser, len) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data_parser}, len} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    {
        InSequence s;
        EXPECT_CALL( *file, write(data_file, len, length_file) )
                .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } ); } ) );
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::FileWriteEvent{ task.fname.c_str(), length_file } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

struct DownloaderSimpleQueue : public DownloaderSimpleFileOpen
{
    DownloaderSimpleQueue()
    {
        EXPECT_CALL( *timer, again() )
                .Times( AtLeast(1) );
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .Times( AtLeast(1) )
                .WillRepeatedly( Invoke(on_tick_handler) );
        EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
                .Times(1);
    }
};

TEST_F(DownloaderSimpleQueue, partial_file_write)
{
    const size_t chunk_size = 1000;
    using Chunk = std::pair< unique_ptr<char[]>, std::size_t >;
    auto generate_chunk = [chunk_size]()
    {
        char* ptr = new char[chunk_size];
        std::random_device rd{};
        std::uniform_int_distribution<unsigned int> dist{0, 255};
        std::generate_n( ptr, chunk_size, [&dist, &rd]() { return static_cast<char>( dist(rd) ); } );
        return Chunk{unique_ptr<char[]>{ptr}, chunk_size};
    };
    vector<Chunk> chunks{backlog};
    std::generate(std::begin(chunks), std::end(chunks), generate_chunk);
    string buffer;
    for (const auto& item : chunks)
        buffer.append(item.first.get(), item.second);

    std::size_t it = 0;
    auto on_data = [this, &it, &chunks]()
    {
        Chunk chunk = std::move( chunks[it++] );
        handler_on_data(std::move(chunk.first), chunk.second);
    };
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( DoAll( InvokeWithoutArgs(on_data),
                                    Return(result) ) );
    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active]() { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );

    for (size_t i = 1; i <= backlog; i++)
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    EXPECT_CALL( *socket, read() )
            .Times(0);

    string buff(chunk_size * backlog, '\0');
    auto buff_replace = [&buff](const char* data, size_t length, size_t offset) { buff.replace(offset, length, data, length); };

    ASSERT_EQ(backlog, 4);

    {
        InSequence s;
        EXPECT_CALL( *file, write(_, chunk_size, 0) ) // first chunk
                .WillOnce( Invoke(buff_replace) );

        EXPECT_CALL( *file, write(_, chunk_size, chunk_size) ) // second cnunk, part one
                .WillOnce( Invoke(buff_replace) );
        EXPECT_CALL( *file, write(_, chunk_size / 2, chunk_size + chunk_size / 2) ) // second chunk, part two
                .WillOnce( Invoke(buff_replace) );

        EXPECT_CALL( *file, write(_, chunk_size, chunk_size * 2) ) // 3
                .WillOnce( Invoke(buff_replace) );

        EXPECT_CALL( *file, write(_, chunk_size, chunk_size * 3) ) // second cnunk, part one
                .WillOnce( Invoke(buff_replace) );
        EXPECT_CALL( *file, write(_, chunk_size / 2, chunk_size * 3 + chunk_size / 2) ) // second chunk, part two
                .WillOnce( Invoke(buff_replace) );
    }

    file->publish( AIO_UVW::FileOpenEvent{task.fname.c_str()} );

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} ); // first chunk write done

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // second chunk, part one
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // second chunk, part two

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} ); // 3, all write

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // 4 chunk, part one
    Mock::VerifyAndClearExpectations(socket.get());
    {
        InSequence s;
        EXPECT_CALL( *socket, active_() )   // continue read
                .WillRepeatedly( Return(false) );
        EXPECT_CALL( *socket, read() )
                .Times(1);
    }
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // 4 chunk, part two

    Mock::VerifyAndClearExpectations(socket.get());
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    EXPECT_TRUE(buffer == buff);

    // Cancel download
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *fs, unlink(_) )
            .Times(0);
    {
        InSequence s;
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    // Delete file
    EXPECT_CALL( *fs, unlink(_) )
            .Times(1);
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
    ASSERT_LE(fs.use_count(), 2);
}

struct DownloaderSimpleBacklog : public DownloaderSimpleQueue
{
    DownloaderSimpleBacklog()
        : written_length{1042}
    {
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::InProgress;
        result.content_length = backlog * written_length;
        auto on_data = [this]() { handler_on_data(unique_ptr<char[]>{ new char[written_length] }, written_length); };
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .Times( AtLeast(1) )
                .WillRepeatedly( DoAll( InvokeWithoutArgs(on_data),
                                        Return(result) ) );
    }

    const size_t written_length;
};

TEST_F(DownloaderSimpleBacklog, socket_stop_on_buffer_filled)
{
    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active] { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );

    for (size_t i = 1; i <= backlog; i++)
    {
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        const auto status = downloader->status();
        EXPECT_EQ(status.size, backlog * written_length);
        EXPECT_EQ(status.downloaded, i * written_length);
    }

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    // Cancel download
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(1);
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );
}

TEST_F(DownloaderSimpleBacklog, socket_read_on_buffer_empty)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times( static_cast<int>(backlog + 2) );
    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active] { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnPointee(&socket_active) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );
    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );
    for (size_t i = 1; i <= backlog; i++)
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );

    Mock::VerifyAndClearExpectations(socket.get());
    // continue read
    {
        InSequence s;
        EXPECT_CALL( *socket, active_() )
                .WillOnce( Return(false) );
        EXPECT_CALL( *socket, read() )
                .Times(1);
    }
    for (size_t i = 1; i <= backlog + 1; i++)
        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );
    Mock::VerifyAndClearExpectations(socket.get());
    // Cancel download
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *fs, unlink(_) )
            .Times(0);
    {
        InSequence s;
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    // Delete file
    EXPECT_CALL( *fs, unlink(_) )
            .Times(1);
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
    ASSERT_LE(fs.use_count(), 2);
}

TEST_F(DownloaderSimpleBacklog, dont_call_read_if_socket_active)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times( AtLeast(1) );
    EXPECT_CALL( *socket, active_() )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return(true) );
    EXPECT_CALL( *socket, read() )
            .Times(0);

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    file->publish( AIO_UVW::FileOpenEvent{task.fname.c_str()} );
    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );

    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    // continue write file
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);
    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    Mock::VerifyAndClearExpectations(file.get());
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    // Cancel download
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(1);
    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *fs, unlink(_) )
            .Times(0);
    {
        InSequence s;
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    // Delete file
    EXPECT_CALL( *fs, unlink(_) )
            .Times(1);
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
    ASSERT_LE(fs.use_count(), 2);
}

struct DownloaderSimpleComplete : public DownloaderSimpleBacklog
{
    DownloaderSimpleComplete()
    {
        EXPECT_CALL( *file, write(_,_,_) )
                .Times( AtLeast(1) );
        // stop read
        bool socket_active = true;
        EXPECT_CALL( *socket, stop() )
                .WillOnce( Invoke( [&socket_active]() { socket_active = false; } ) );
        EXPECT_CALL( *socket, active_() )
                .WillRepeatedly( ReturnPointee(&socket_active) );

        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );
        for (size_t i = 1; i <= backlog; i++)
            socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        Mock::VerifyAndClearExpectations(socket.get());
        // continue read
        {
            InSequence s;
            EXPECT_CALL( *socket, active_() )
                    .WillOnce( Return(false) );
            EXPECT_CALL( *socket, read() )
                    .Times(1);
        }
        for (size_t i = 1; i <= backlog + 1; i++)
            file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );
        Mock::VerifyAndClearExpectations(socket.get());
        // done, close connection
        EXPECT_CALL( *socket, active_() )
                .WillRepeatedly( Return(true) );
        EXPECT_CALL( *socket, stop() )
                .Times(0);
        for (size_t i = 1; i <= backlog - 2; i++)
            socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        Mock::VerifyAndClearExpectations(http_parser);
        bool timer_stoped = false;
        EXPECT_CALL( *timer, stop() )
                .Times( AnyNumber() )
                .WillRepeatedly( Invoke( [&timer_stoped]() { timer_stoped = true; } ) );
        EXPECT_CALL( *socket, shutdown() )
                .Times(1);
        EXPECT_CALL( *timer, close_() )
                .Times(1);
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::Done;
        auto on_data = [this]() { handler_on_data(unique_ptr<char[]>{}, written_length); };
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .Times(1)
                .WillRepeatedly( DoAll( InvokeWithoutArgs(on_data),
                                        Return(result) ) );
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(http_parser);
        if (!timer_stoped)
            timer->publish( AIO_UVW::TimerEvent{} ); // timer should be cleared or stoped
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        // done, close file
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *loop, resource_FsReqMock() ) // dont delete file
                .Times(0);
        EXPECT_CALL( *socket, read() )
                .Times(0);
        for (size_t i = 1; i <= backlog - 1; i++)
            file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), written_length} );
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(socket.get());
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        // close handles
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
};

TEST_F(DownloaderSimpleComplete, close_socket_before_file)
{
    socket->publish( AIO_UVW::ShutdownEvent{} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Done);
}

TEST_F(DownloaderSimpleComplete, close_file_before_socket)
{
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ShutdownEvent{} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Done);
}

TEST_F(DownloaderSimpleComplete, close_socket_before_file_error)
{
    socket->publish( AIO_UVW::ShutdownEvent{} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    EXPECT_CALL( *socket, close_() )
            .Times( AnyNumber() );
    EXPECT_CALL( *timer, close_() )
            .Times( AnyNumber() );
    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
}

TEST_F(DownloaderSimpleComplete, file_error)
{
    EXPECT_CALL( *timer, close_() )
            .Times( AnyNumber() );
    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
}

