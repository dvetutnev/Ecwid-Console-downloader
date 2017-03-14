#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "http.h"

extern "C" {
#include <http_parser.h> // remove ???
}

using namespace std;

TEST(http_parser, incomplete_body)
{
    const string buf ="\
HTTP/1.1 200 OK\r\n\
Server: nginx/1.6.2\r\n\
Date: Fri, 10 Mar 2017 18:11:04 GMT\r\n\
Content-Type: text/pain\r\n\
Content-Length: 12\r\n\
Last-Modified: Fri, 10 Mar 2017 18:09:54 GMT\r\n\
Connection: keep-alive\r\n\
ETag: \"58c2fb69-c\"\r\n\
Accept-Ranges: bytes\r\n\
\r\n\
Hello world\
";

//    std::cout << "buf.size() => " << buf.size() << std::endl;

    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    struct http_parser_data
    {
        size_t content_length = 0;
        size_t body_length = 0;
        size_t http_code = 0;
        string http_status;
    };
    http_parser_data data;
    parser.data = &data;

    http_parser_settings settings;
    http_parser_settings_init(&settings);

    auto on_status = [](http_parser* parser, const char* at, size_t len) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser->data);
        data_ptr->http_code = parser->status_code;
        data_ptr->http_status.append(at, len);
        return 0;
    };
    settings.on_status = on_status;

    auto on_headers_complete = [](http_parser* parser) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser->data);
        data_ptr->content_length = parser->content_length;
        return 0;
    };
    settings.on_headers_complete = on_headers_complete;

    auto on_boby = [](http_parser* parser, const char* at, size_t len) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser->data);
        data_ptr->body_length = len;
        return 0;
    };
    settings.on_body = on_boby;

    const size_t nparsed = http_parser_execute(&parser, &settings, buf.data(), buf.size());

//    const char* str = http_errno_name( static_cast<enum http_errno>(parser.http_errno) );
//    std::cout << str << std::endl;

    cout << "http_status => " << data.http_status << endl;

    ASSERT_EQ(nparsed, buf.size());
    ASSERT_EQ(parser.status_code, 200u);

    ASSERT_EQ(data.content_length, 12u);
    ASSERT_EQ(data.body_length, 11u);
    ASSERT_EQ(data.http_code, 200u);
}

TEST(HttpParser__BodyDecoderContentLength, normal)
{
    const string s1 = "QWERTYUIOP";
    const string s2 = "ASDFGHJKL";

    HttpParser::BodyDecoderContentLength body_decoder{ s1.size() + s2.size() };

    auto res1 = body_decoder( s1.data(), s1.length() );
    ASSERT_FALSE(res1.is_done);
    ASSERT_TRUE(res1.data.buffer);
    const string str1{ res1.data.buffer.get(), res1.data.length };
    ASSERT_EQ(str1, s1);

    auto res2 = body_decoder( s2.data(), s2.length() );
    ASSERT_TRUE(res2.is_done);
    ASSERT_TRUE(res2.data.buffer);
    const string str2{ res2.data.buffer.get(), res2.data.length };
    ASSERT_EQ(str2, s2);
}

TEST(HttpParser__BodyDecoderContentLength, input_data_larger_content_length)
{
    const string s1 = "QWERTYUIOP";
    const string s2 = "ASDFGHJKL";
    const string s2_ = s2 + "ZXCVBNM";

    HttpParser::BodyDecoderContentLength body_decoder{ s1.size() + s2.size() };

    auto res1 = body_decoder( s1.data(), s1.length() );
    ASSERT_FALSE(res1.is_done);
    ASSERT_TRUE(res1.data.buffer);
    const string str1{ res1.data.buffer.get(), res1.data.length };
    ASSERT_EQ(str1, s1);

    auto res2 = body_decoder( s2_.data(), s2_.length() );
    ASSERT_TRUE(res2.is_done);
    ASSERT_TRUE(res2.data.buffer);
    const string str2{ res2.data.buffer.get(), res2.data.length };
    ASSERT_EQ(str2, s2);

    auto res3 = body_decoder( s2_.data(), s2_.length() );
    ASSERT_TRUE(res3.is_done);
    ASSERT_FALSE(res3.data.buffer);
}
