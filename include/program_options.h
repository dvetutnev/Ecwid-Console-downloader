#pragma once

#include <string>

struct ProgramOptions
{
    std::size_t concurrency;
    std::size_t limit;
    std::string path;
    std::string task_fname;
};

const ProgramOptions parse_program_options(int argc, char* argv[]);
