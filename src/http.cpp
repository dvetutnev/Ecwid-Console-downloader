#include "http.h"

#include <map>

extern "C" {
    #include <http_parser.h>
}

using namespace std;

static const map<string, size_t> proto_default_port{
    { "http", 80u },
    { "https", 443u }
};

shared_ptr<HttpParser::UriParseResult> HttpParser::uri_parse(const string& uri)
{
    shared_ptr<UriParseResult> ret{};
    struct http_parser_url result;
    http_parser_url_init(&result);

    if ( http_parser_parse_url( uri.data(), uri.size(), 0, &result ) == 0 )
    {
        const string& proto = uri.substr( result.field_data[UF_SCHEMA].off, result.field_data[UF_SCHEMA].len );
        if ( proto_default_port.find(proto) == proto_default_port.end() )
            return ret;

        ret = make_shared<UriParseResult>();

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
