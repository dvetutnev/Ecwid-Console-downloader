#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "downloader_simple.h"
#include "aio_uvw.h"
#include "aio_loop_mock.h"
#include "on_tick_mock.h"
#include "http.h"

using namespace std;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::ByMove;
using ::testing::Mock;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::DoDefault;
using ::testing::Invoke;

/*------- GetAddrInfoReqMock -------*/
using ErrorEvent = AIO_UVW::ErrorEvent;
using AddrInfoEvent = AIO_UVW::AddrInfoEvent;

namespace GetAddrInfoReqMock_interanl {

template< typename T >
void once(GetAddrInfoReqMock&, Callback<T, GetAddrInfoReqMock>) {}

template<>
void once<ErrorEvent>(GetAddrInfoReqMock&, Callback<ErrorEvent, GetAddrInfoReqMock>);
template<>
void once<AddrInfoEvent>(GetAddrInfoReqMock&, Callback<AddrInfoEvent, GetAddrInfoReqMock>);

}
struct GetAddrInfoReqMock
{
    template< typename T >
    void once( Callback<T, GetAddrInfoReqMock> cb ) { GetAddrInfoReqMock_interanl::once<T>(*this, cb); }

    MOCK_METHOD1( once_ErrorEvent, void( Callback<ErrorEvent, GetAddrInfoReqMock> ) );
    MOCK_METHOD1( once_AddrInfoEvent, void( Callback<AddrInfoEvent, GetAddrInfoReqMock> ) );
    MOCK_METHOD1( getNodeAddrInfo, void(string) );
};
namespace GetAddrInfoReqMock_interanl {

template<>
void once<ErrorEvent>(GetAddrInfoReqMock& self, Callback<ErrorEvent, GetAddrInfoReqMock> cb) { self.once_ErrorEvent(cb); }
template<>
void once<AddrInfoEvent>(GetAddrInfoReqMock& self, Callback<AddrInfoEvent, GetAddrInfoReqMock> cb) { self.once_AddrInfoEvent(cb); }

}

struct AIO_Mock
{
    using Loop = LoopMock;
    using ErrorEvent = ErrorEvent;
    using AddrInfoEvent = AddrInfoEvent;
    using GetAddrInfoReq = GetAddrInfoReqMock;
};

/*------- HttpParserMock -------*/
using UriParseResult = HttpParser::UriParseResult;
struct HttpParserMock
{
    using UriParseResult = HttpParser::UriParseResult;
    static shared_ptr<HttpParserMock> instance_uri_parse;
    static unique_ptr<UriParseResult> uri_parse(const string& uri) { return instance_uri_parse->uri_parse_(uri); }
    MOCK_METHOD1(uri_parse_, unique_ptr<UriParseResult>(const string&));
};
shared_ptr<HttpParserMock> HttpParserMock::instance_uri_parse;

/*------- tests implementation -------*/

TEST(DownloaderSimple, uri_parse_falied)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = new HttpParserMock;
    HttpParserMock::instance_uri_parse = shared_ptr<HttpParserMock>{instance_uri_parse};

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    const string bad_uri = "bad_uri";
    const Task task{bad_uri, "invalid_fname"};
    EXPECT_CALL( *instance_uri_parse, uri_parse_(bad_uri) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove(unique_ptr<UriParseResult>{}) ) );
    EXPECT_CALL( *on_tick, invoke(_) )
            .Times(0);

    ASSERT_FALSE( downloader->run(task) );

    HttpParserMock::instance_uri_parse.reset();
}

TEST(DownloaderSimple, host_resolve_failed)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = new HttpParserMock;
    HttpParserMock::instance_uri_parse = shared_ptr<HttpParserMock>{instance_uri_parse};

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    const string host = "www.internet.org";
    UriParseResult uri_parse_result;
    uri_parse_result.host = host;
    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( make_unique<UriParseResult>(uri_parse_result) ) ) );
    EXPECT_CALL( *on_tick, invoke(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    Callback<ErrorEvent, GetAddrInfoReqMock> handler_resolver_error;

    Sequence s1, s2;
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock())
            .InSequence(s1, s2)
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, once_ErrorEvent(_) )
            .InSequence(s1)
            .WillOnce( SaveArg<0>(&handler_resolver_error) );
    EXPECT_CALL( *resolver, once_AddrInfoEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
            .InSequence(s1, s2)
            .WillOnce( DoDefault() );

    ASSERT_TRUE( downloader->run( Task{} ) );
    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *on_tick, invoke( static_pointer_cast<Downloader>(downloader) ) )
            .Times(1);
    handler_resolver_error( ErrorEvent{ static_cast<int>(UV_EAI_NONAME) }, *resolver );

    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Failed);
    cout << "status.state_str => " << status.state_str << endl;

    HttpParserMock::instance_uri_parse.reset();
}

TEST(DownloaderSimple, dont_invoke_tick_if_resolver_failed_run)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = new HttpParserMock;
    HttpParserMock::instance_uri_parse = shared_ptr<HttpParserMock>{instance_uri_parse};

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( make_unique<UriParseResult>() ) ) );
    EXPECT_CALL( *on_tick, invoke(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    Callback<ErrorEvent, GetAddrInfoReqMock> handler_resolver_error;

    Sequence s1, s2;
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock())
            .InSequence(s1, s2)
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, once_ErrorEvent(_) )
            .InSequence(s1)
            .WillOnce( SaveArg<0>(&handler_resolver_error) );
    EXPECT_CALL( *resolver, once_AddrInfoEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *resolver, getNodeAddrInfo(_) )
            .InSequence(s1, s2)
            .WillOnce( Invoke( std::bind( std::ref(handler_resolver_error),
                                          ErrorEvent{ static_cast<int>(UV_ENOSYS) },
                                          std::ref(*resolver)
                                          ) ) );

    ASSERT_FALSE( downloader->run( Task{} ) );
    Mock::VerifyAndClearExpectations(instance_uri_parse);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    HttpParserMock::instance_uri_parse.reset();
}

