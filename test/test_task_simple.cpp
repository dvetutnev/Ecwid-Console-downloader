#include <gtest/gtest.h>

#include "task_simple.h"
#include <sstream>

using ::std::string;
using ::std::stringstream;
using ::std::endl;

TEST(TaskListSimple, normal)
{
    const string path = "/home/";

    const string uri_1 = "http://internet.org/archive.bin";
    const string fname_1 = "downloaded_file_1.zip";
    const string uri_2 = "http://internet.org/download/";
    const string fname_2 = "New_file.zip";

    stringstream stream;
    stream << uri_1 << " " << fname_1 << endl;
    stream << uri_2 << " " << fname_2 << endl;

    TaskListSimple task_list{stream, path};

    auto task_1 = task_list.get();
    ASSERT_TRUE(task_1);
    ASSERT_EQ(task_1->uri, uri_1);
    ASSERT_EQ(task_1->fname, path + fname_1);

    auto task_2 = task_list.get();
    ASSERT_TRUE(task_2);
    ASSERT_EQ(task_2->uri, uri_2);
    ASSERT_EQ(task_2->fname, path + fname_2);
}

TEST(TaskListSimple, null_if_eof)
{
    stringstream stream;
    stream << "http://internet.org/archive.bin" << " " << "downloaded_file_1.zip" << endl;

    TaskListSimple task_list{stream, std::string{} };

    task_list.get();
    auto task_2 = task_list.get();
    ASSERT_FALSE(task_2);
    auto task_3 = task_list.get();
    ASSERT_FALSE(task_3);
}

TEST(TaskListSimple, skip_line_empty)
{
    const string uri_1 = "http://internet.org/archive.bin";
    const string fname_1 = "downloaded_file_1.zip";
    const string uri_2 = "http://internet.org/download/";
    const string fname_2 = "New_file.zip";

    stringstream stream;
    stream << uri_1 << " " << fname_1 << endl;
    stream << endl;
    stream << uri_2 << " " << fname_2 << endl;

    TaskListSimple task_list{stream, std::string{} };

    auto task_1 = task_list.get();
    ASSERT_TRUE(task_1);
    ASSERT_EQ(task_1->uri, uri_1);
    ASSERT_EQ(task_1->fname, fname_1);

    auto task_2 = task_list.get();
    ASSERT_TRUE(task_2);
    ASSERT_EQ(task_2->uri, uri_2);
    ASSERT_EQ(task_2->fname, fname_2);
}

TEST(TaskListSimple, skip_line_not_complete)
{
    const string uri_1 = "http://internet.org/archive.bin";
    const string fname_1 = "downloaded_file_1.zip";
    const string uri_2 = "http://internet.org/download/";
    const string fname_2 = "New_file.zip";

    stringstream stream;
    stream << uri_1 << " " << fname_1 << endl;
    stream << "xyz" << endl;
    stream << uri_2 << " " << fname_2 << endl;


    TaskListSimple task_list{stream, std::string{} };

    auto task_1 = task_list.get();
    ASSERT_TRUE(task_1);
    ASSERT_EQ(task_1->uri, uri_1);
    ASSERT_EQ(task_1->fname, fname_1);

    auto task_2 = task_list.get();
    ASSERT_TRUE(task_2);
    ASSERT_EQ(task_2->uri, uri_2);
    ASSERT_EQ(task_2->fname, fname_2);
}

TEST(TaskListSimple, ignore_whitespace_charters)
{
    const string uri = "http://internet.org/archive.bin";
    const string fname = "downloaded_file_1.zip";

    stringstream stream;
    stream << "  " << uri << "  " << fname << "  " << std::endl;

    TaskListSimple task_list{stream, string{} };

    auto task = task_list.get();
    ASSERT_TRUE(task);
    ASSERT_EQ(task->uri, uri);
    ASSERT_EQ(task->fname, fname);
}

TEST(TaskListSimple, ignore_additional_word)
{
    const string uri = "http://internet.org/archive.bin";
    const string fname = "downloaded_file_1.zip";

    stringstream stream;
    stream << uri << " " << fname << " xyz" << std::endl;

    TaskListSimple task_list{stream, string{} };

    auto task = task_list.get();
    ASSERT_TRUE(task);
    ASSERT_EQ(task->uri, uri);
    ASSERT_EQ(task->fname, fname);
}

TEST(TaskListSimple, constructor)
{
    stringstream stream;
    string path;

    TaskListSimple path_lvalue{stream, path};
    TaskListSimple path_rvalue{stream, move(path) };
    TaskListSimple path_const_char{stream, "/path/" };
    //TaskListSimple invalid_path_type{stream, 42};
}
