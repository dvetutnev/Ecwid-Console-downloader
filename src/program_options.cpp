#include "program_options.h"
#include <docopt.h>

#include <iostream>
#include <regex>

using ::std::string;
using ::std::move;
using ::std::size_t;
using ::std::regex;
using ::std::regex_search;
using ::std::stol;
using ::std::cout;
using ::std::endl;
using ::std::runtime_error;
using ::std::invalid_argument;
using ::std::exit;

static const string usage =
R"(Ecwid-Console-downloader https://github.com/Ecwid/new-job/blob/master/Console-downloader.md

        Usage:
         Ecwid-Console-downloader -n <concurrency> -l <speed limit> -o <output path> -f <task file name>
         Ecwid-Console-downloader (-h | --help)

        Options:
         -h --help  Show this message
)";

const ProgramOptions parse_program_options(int argc, char* argv[])
{
    auto options = docopt::docopt(usage, {argv + 1, argv + argc} );

    size_t concurrency;
    size_t speed_limit;

    try {
        auto c = options["<concurrency>"].asLong();
        if (c < 0)
            throw runtime_error{"Invalid concurrency"};
        concurrency = static_cast<size_t>(c);

        string s = options["<speed limit>"].asString();
        regex re{"^\\d+(k|K|m|M)?$"};
        if ( !regex_search(s, re) )
            throw runtime_error{"Invalid sped limit"};

        switch ( s.back() )
        {
        case 'k':
        case 'K':
            speed_limit = stoul( s.substr(0, s.length() - 1) ) * 1024;
            break;
        case 'm':
        case 'M':
            speed_limit = stoul( s.substr(0, s.length() - 1) ) * 1024 * 1024;
            break;
        default:
            speed_limit = stoul(s);
            break;
        }

        if (speed_limit == 0)
            throw runtime_error{"Invalid sped limit"};

    } catch (runtime_error& e) {
        cout << e.what() << endl << usage;
        exit(1);
    }

    return ProgramOptions{ concurrency, speed_limit, move( options["<output path>"].asString() ), move( options["<task file name>"].asString() ) };
}
