#include "http.h"

#include <cassert>
#include <limits>
#include <algorithm>

using namespace std;

/* response parser */

using State = HttpParser::ResponseParseResult::State;

const HttpParser::ResponseParseResult HttpParser::response_parse(unique_ptr<char[]> data, size_t length)
{
    http_parser_execute(&parser, &parser_settings, data.get(), length);

    if ( !(parser.http_errno == HPE_OK || parser.http_errno == HPE_PAUSED) )
    {
        result.state = State::Error;
        result.err_str = http_errno_description( static_cast<enum http_errno>(parser.http_errno) );
    }

    return  result;
}

int HttpParser::on_status(http_parser* parser, const char* data, size_t length)
{
    auto self = static_cast<HttpParser*>(parser->data);

    switch (parser->status_code)
    {
    case 200:
    case 202:
    case 203:
        break;

    case 301:
    case 302:
    case 303:
        self->redirect = true;
        break;

    default:
        self->result.err_str = to_string(parser->status_code) + " " + string{data, length};
        self->stop(State::Error);
        break;
    }

    return 0;
}

int HttpParser::on_header_field(http_parser* parser, const char* data, size_t length)
{
    auto self = static_cast<HttpParser*>(parser->data);

    switch (self->mode_header)
    {
    case ModeHeader::Field:
        self->field_header.append(data, length);
        break;

    case ModeHeader::Value:
        self->headers.emplace( std::move(self->field_header), std::move(self->value_header) );
        self->field_header = string{data, length};
        self->mode_header = ModeHeader::Field;
        break;
    }

    return 0;
}

int HttpParser::on_header_value(http_parser* parser, const char* data, size_t length)
{
    auto self = static_cast<HttpParser*>(parser->data);

    switch (self->mode_header)
    {
    case ModeHeader::Value:
        self->value_header.append(data, length);
        break;

    case ModeHeader::Field:
        self->value_header = string{data, length};
        self->mode_header = ModeHeader::Value;
        break;
    }

    return 0;
}

int HttpParser::on_headers_complete(http_parser* parser)
{
    auto self = static_cast<HttpParser*>(parser->data);

    if ( !(self->field_header.empty()) )
        self->headers.emplace( std::move(self->field_header), std::move(self->value_header) );

    if (self->redirect)
    {
        auto it = self->headers.find("Location");
        if ( it != std::end(self->headers) )
        {
            self->result.redirect_uri = std::move( self->headers["Location"] );
            self->stop(State::Redirect);
        } else
        {
            self->result.err_str = "Invalid redirect, missing Location header";
            self->stop(State::Error);
        }
    } else
    {
        assert( parser->content_length <= numeric_limits<std::size_t>::max() );
        self->result.content_length = parser->content_length;
    }

    return 0;
}

int HttpParser::on_body(http_parser* parser, const char* data, size_t length)
{
    auto buffer = make_unique<char[]>(length);
    std::copy_n( data, length, buffer.get() );

    auto self = static_cast<HttpParser*>(parser->data);
    self->cb_on_data(std::move(buffer), length);

    return 0;
}

int HttpParser::on_message_complete(http_parser* parser)
{
    auto self = static_cast<HttpParser*>(parser->data);
    self->stop(State::Done);
    return 0;
}

void HttpParser::stop(State state)
{
    result.state = state;
    http_parser_pause(&parser, 1u);
}

/* uri parser */

static const map<string, unsigned short> proto_default_port{
    { "http", 80u },
    { "https", 443u }
};

std::unique_ptr<HttpParser::UriParseResult> HttpParser::uri_parse(const string& uri)
{
    unique_ptr<UriParseResult> ret{};
    struct http_parser_url result;
    http_parser_url_init(&result);

    if ( http_parser_parse_url( uri.data(), uri.size(), 0, &result ) == 0 )
    {
        const string& proto = uri.substr( result.field_data[UF_SCHEMA].off, result.field_data[UF_SCHEMA].len );
        if ( proto_default_port.find(proto) == proto_default_port.end() )
            return ret;

        ret = make_unique<UriParseResult>();

        ret->proto = proto;
        ret->host = uri.substr( result.field_data[UF_HOST].off, result.field_data[UF_HOST].len );

        ret->port = ( result.field_set & (1 << UF_PORT) ) ? result.port : proto_default_port.at( ret->proto );

        ret->query = ( result.field_set & (1 << UF_PATH) ) ? uri.substr( result.field_data[UF_PATH].off, result.field_data[UF_PATH].len ) : "/";
        if ( result.field_set & (1 << UF_QUERY) )
            ret->query += "?" + uri.substr( result.field_data[UF_QUERY].off, result.field_data[UF_QUERY].len );
        if ( result.field_set & (1 << UF_FRAGMENT) )
            ret->query += "#" + uri.substr( result.field_data[UF_FRAGMENT].off, result.field_data[UF_FRAGMENT].len );

        if ( result.field_set & (1 << UF_USERINFO) )
        {
            const string& userinfo = uri.substr( result.field_data[UF_USERINFO].off, result.field_data[UF_USERINFO].len );
            auto pos = userinfo.find(":");
            ret->username = userinfo.substr(0, pos);
            if ( pos != userinfo.npos )
                ret->password = userinfo.substr(pos + 1);
        }
    }
    return ret;
}
