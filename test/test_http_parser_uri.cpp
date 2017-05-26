#include <gtest/gtest.h>

#include "http.h"

using namespace std;

TEST(uri_parse, normal)
{
    const string proto = "http";
    const string host = "www.internet.org";
    const unsigned short port = 8080;
    const string query = "/path?id=iidd&mode=full#42";
    const string username = "username";
    const string password = "password";

    const string uri = proto + "://" + username + ":" + password + "@" + host + ":" + to_string(port) + query;

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->proto, proto);
    ASSERT_EQ(result->host, host);
    ASSERT_EQ(result->port, port);
    ASSERT_EQ(result->query, query);
    ASSERT_EQ(result->username, username);
    ASSERT_EQ(result->password, password);
}

TEST(uri_parse, invalid_uri)
{
    const string uri = "http://username:password@www.inte#net.org:8080/path?id=iidd&mode=full#42";
    auto result = HttpParser::uri_parse(uri);
    ASSERT_FALSE(result);
}

TEST(uri_parse, unknow_proto)
{
    const string uri = "httpp://username:password@www.intenet.org:8080/path?id=iidd&mode=full#42";
    auto result = HttpParser::uri_parse(uri);
    ASSERT_FALSE(result);
}

TEST(uri_parse, without_fragment)
{
    const string query = "/path?id=iidd&mode=full";
    const string uri = "http://username:password@www.intenet.org:8080" + query;

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->query, query);
}

TEST(uri_parse, without_query)
{
    const string query = "/path#42";
    const string uri = "http://username:password@www.intenet.org:8080" + query;

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->query, query);
}

TEST(uri_parse, without_path)
{
    const string query = "/?id=iidd&mode=full#42";
    const string uri = "http://username:password@www.intenet.org:8080" + query;

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->query, query);
}

TEST(uri_parse, default_path)
{
    const string query = "?id=iidd&mode=full#42";
    const string uri = "http://username:password@www.intenet.org:8080" + query;

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->query, "/" + query);
}

TEST(uri_parse, no_path_and_params)
{
    const string uri = "http://username:password@www.intenet.org:8080";

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->query, "/");
}

TEST(uri_parse, without_password)
{
    const string username = "username";
    const string uri = "http://" + username + "@www.intenet.org:8080/path?id=iidd&mode=full#42";

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->username, username);
    ASSERT_EQ(result->password.size(), 0u);
}

TEST(uri_parse, without_username)
{
    const string uri = "http://www.intenet.org:8080/path?id=iidd&mode=full#42";

    auto result = HttpParser::uri_parse(uri);

    ASSERT_TRUE(result);
    ASSERT_EQ(result->username.size(), 0u);
    ASSERT_EQ(result->password.size(), 0u);
}

TEST(uri_parse, default_port_80_for_http)
{
    const string uri = "http://username:password@www.intenet.org/path?id=iidd&mode=full#42";
    auto result = HttpParser::uri_parse(uri);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->port, 80u);
}

TEST(uri_parse, default_port_443_for_https)
{
    const string uri = "https://username:password@www.intenet.org/path?id=iidd&mode=full#42";
    auto result = HttpParser::uri_parse(uri);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->port, 443u);
}
