#pragma once

#include <memory>
#include <string>

class HttpParser
{
public:
    struct UriParseResult;
    static std::shared_ptr<UriParseResult> uri_parse(const std::string&);

};

struct HttpParser::UriParseResult
{
    std::string proto;
    std::string host;
    std::size_t port;
    std::string query;
    std::string username;
    std::string password;
};
