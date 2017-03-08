#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "task_simple.h"
#include <sstream>

using namespace std;

TEST(TaskListSimple, normal)
{
    const string path{"/home/"};

    const Task t1{
        string{"http://internet.org/archive.bin"},
        string{"downloaded_file_1.zip"}
    };
    const Task t2{
        string{"http://internet.org/download/"},
        string{"New_file.zip"}
    };

    stringstream stream;
    stream << t1.uri << " " << t1.fname << std::endl;
    stream << t2.uri << " " << t2.fname << std::endl;

    TaskListSimple task_list{stream, path};

    auto t1_ptr = task_list.get();
    ASSERT_TRUE(t1_ptr);
    ASSERT_EQ(t1_ptr->uri, t1.uri);
    ASSERT_EQ(t1_ptr->fname, path + t1.fname);

    auto t2_ptr = task_list.get();
    ASSERT_TRUE(t2_ptr);
    ASSERT_EQ(t2_ptr->uri, t2.uri);
    ASSERT_EQ(t2_ptr->fname, path + t2.fname);
}

TEST(TaskListSimple, null_if_eof)
{
    const Task t1{
        string{"http://internet.org/archive.bin"},
        string{"downloaded_file_1.zip"}
    };

    stringstream stream;
    stream << t1.uri << " " << t1.fname << std::endl;

    TaskListSimple task_list{stream, std::string{} };

    task_list.get();
    auto t2_ptr = task_list.get();
    ASSERT_FALSE(t2_ptr);
    auto t3_ptr = task_list.get();
    ASSERT_FALSE(t3_ptr);
}

TEST(TaskListSimple, skip_line_empty)
{
    const Task t1{
        string{"http://internet.org/archive.bin"},
        string{"downloaded_file_1.zip"}
    };
    const Task t2{
        string{"http://internet.org/download/"},
        string{"New_file.zip"}
    };

    stringstream stream;
    stream << t1.uri << " " << t1.fname << std::endl;
    stream << std::endl;
    stream << t2.uri << " " << t2.fname << std::endl;

    TaskListSimple task_list{stream, std::string{} };

    auto t1_ptr = task_list.get();
    ASSERT_TRUE(t1_ptr);
    ASSERT_EQ(t1_ptr->uri, t1.uri);
    ASSERT_EQ(t1_ptr->fname, t1.fname);

    auto t2_ptr = task_list.get();
    ASSERT_TRUE(t2_ptr);
    ASSERT_EQ(t2_ptr->uri, t2.uri);
    ASSERT_EQ(t2_ptr->fname, t2.fname);
}

TEST(TaskListSimple, skip_line_not_complete)
{
    const Task t1{
        string{"http://internet.org/archive.bin"},
        string{"downloaded_file_1.zip"}
    };
    const Task t2{
        string{"http://internet.org/download/"},
        string{"New_file.zip"}
    };

    stringstream stream;
    stream << t1.uri << " " << t1.fname << std::endl;
    stream << "xyz" << std::endl;
    stream << t2.uri << " " << t2.fname << std::endl;

    TaskListSimple task_list{stream, std::string{} };

    auto t1_ptr = task_list.get();
    ASSERT_TRUE(t1_ptr);
    ASSERT_EQ(t1_ptr->uri, t1.uri);
    ASSERT_EQ(t1_ptr->fname, t1.fname);

    auto t2_ptr = task_list.get();
    ASSERT_TRUE(t2_ptr);
    ASSERT_EQ(t2_ptr->uri, t2.uri);
    ASSERT_EQ(t2_ptr->fname, t2.fname);
}

TEST(TaskListSimple, ignore_whitespace_charters)
{
    const Task t1{
        string{"http://internet.org/archive.bin"},
        string{"downloaded_file_1.zip"}
    };

    stringstream stream;
    stream << "  " << t1.uri << "  " << t1.fname << "  " << std::endl;

    TaskListSimple task_list{stream, string{} };

    auto t1_ptr = task_list.get();
    ASSERT_TRUE(t1_ptr);
    ASSERT_EQ(t1_ptr->uri, t1.uri);
    ASSERT_EQ(t1_ptr->fname, t1.fname);
}

TEST(TaskListSimple, ignore_additional_word)
{
    const Task t1{
        string{"http://internet.org/archive.bin"},
        string{"downloaded_file_1.zip"}
    };

    stringstream stream;
    stream << t1.uri << " " << t1.fname << " xyz" << std::endl;

    TaskListSimple task_list{stream, string{} };

    auto t1_ptr = task_list.get();
    ASSERT_TRUE(t1_ptr);
    ASSERT_EQ(t1_ptr->uri, t1.uri);
    ASSERT_EQ(t1_ptr->fname, t1.fname);
}
