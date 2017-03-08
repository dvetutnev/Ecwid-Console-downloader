#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "downloader_mock.h"
#include "factory_mock.h"
#include "task_mock.h"

#include "on_tick_simple.h"

#include <algorithm>
#include <random>

using namespace std;

using ::testing::ReturnRef;
using ::testing::Return;
using ::testing::AtLeast;
using ::testing::_;
using ::testing::DoAll;
using ::testing::SaveArg;

TEST(OnTickSimple, Downloader_is_OnTheGo)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::OnTheGo;
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .Times(0);

    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(_) )
            .Times(0);

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(used_job.downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    it = std::find_if( it_begin, it_end,
                       [&used_job, &used_task](const auto& job) { return job == used_job && *(job.task) == used_task; } );
    ASSERT_NE( it, it_end );

    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 2u );
}

void Downloader_normal_completion(StatusDownloader::State state)
{
    StatusDownloader status;
    status.state = state;
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    const Task next_task{ string{"http://internet.org/next"}, string{"fname_next.zip"} };
    auto next_task_ptr = std::make_shared<Task>(next_task);
    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return(next_task_ptr) );

    auto next_downloader = make_shared<DownloaderMock>();
    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(next_task) )
            .WillOnce( Return(next_downloader) );

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(used_downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Insert next Job
    Job next_job{ next_task_ptr, next_downloader };
    it = std::find_if( it_begin, it_end,
                       [&next_job, &next_task](const auto& job) { return job == next_job && *(job.task) == next_task; } );
    ASSERT_NE( it, it_end );

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 2u );
}

TEST(OnTickSimple, Downloader_is_Done)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Done");
        Downloader_normal_completion(StatusDownloader::State::Done);
    }
}

TEST(OnTickSimple, Downloader_is_Failed)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Failed");
        Downloader_normal_completion(StatusDownloader::State::Failed);
    }
}

void Downloader_completion_Factory_is_null(StatusDownloader::State state)
{
    StatusDownloader status;
    status.state = state;
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .Times(0);

    auto factory = std::make_shared<FactoryMock>();

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};

    factory.reset();
    on_tick(used_downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 1u );
}

TEST(OnTickSimple, Downloader_is_Done_Factory_is_null)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Done");
        Downloader_completion_Factory_is_null(StatusDownloader::State::Done);
    }
}

TEST(OnTickSimple, Downloader_is_Failed_Factory_is_null)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Failed");
        Downloader_completion_Factory_is_null(StatusDownloader::State::Failed);
    }
}

void Downloader_completion_no_Task(StatusDownloader::State state)
{
    StatusDownloader status;
    status.state = state;
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return( shared_ptr<Task>{} ) );

    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(_) )
            .Times(0);

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(used_downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 1u );
}

TEST(OnTickSimple, Downloader_is_Done_no_Task)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Done");
        Downloader_completion_no_Task(StatusDownloader::State::Done);
    }
}

TEST(OnTickSimple, Downloader_is_Failed_no_Task)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Failed");
        Downloader_completion_no_Task(StatusDownloader::State::Failed);
    }
}

void Downloader_completion_Factory_returning_null_no_Task(StatusDownloader::State state)
{
    StatusDownloader status;
    status.state = state;
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    const Task first_bad_task{ string{"first_bad_uri"}, string{"first_bad_fname"} };
    const Task second_bad_task{ string{"second_bad_uri"}, string{"second_bad_fname"} };
    auto first_bad_task_ptr = make_shared<Task>(first_bad_task);
    auto second_bad_task_ptr = make_shared<Task>(second_bad_task);
    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return(first_bad_task_ptr) )
            .WillOnce( Return(second_bad_task_ptr) )
            .WillOnce( Return(shared_ptr<Task>{}) );

    Task first_call_factory_task, second_call_factory_task;
    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(_) )
            .WillOnce( DoAll( SaveArg<0>(&first_call_factory_task), Return(shared_ptr<Downloader>{}) ) )
            .WillOnce( DoAll( SaveArg<0>(&second_call_factory_task), Return(shared_ptr<Downloader>{}) ) );

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(used_downloader);

    ASSERT_EQ( first_call_factory_task, first_bad_task );
    ASSERT_EQ( second_call_factory_task, second_bad_task );

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 1u );
}

TEST(OnTickSimple, Downloader_is_Done_Factory_returning_null_no_Task)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Done");
        Downloader_completion_Factory_returning_null_no_Task(StatusDownloader::State::Done);
    }
}

TEST(OnTickSimple, Downloader_is_Failed_Factory_returning_null_no_Task)
{
    {
        SCOPED_TRACE("StatusDownloader::State::Failed");
        Downloader_completion_Factory_returning_null_no_Task(StatusDownloader::State::Failed);
    }
}

TEST(OnTickSimple, Downloader_is_Redirect)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .Times(0);

    const Task redirect_task{ string{status.redirect_uri}, string{used_job.task->fname} };
    auto redirect_downloader = make_shared<DownloaderMock>();
    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(redirect_task) )
            .WillOnce( Return(redirect_downloader) );

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(used_downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Update Downloader & Task.uri in current Job
    it = std::find_if( it_begin, it_end,
                       [&used_job, &redirect_task, &redirect_downloader](const auto& job)
    {
        return job.task == used_job.task && *(job.task) == redirect_task && job.downloader == redirect_downloader;
    } );
    ASSERT_NE(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 2u );
}

TEST(OnTickSimple, Downloader_is_Redirect_Factory_is_null)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .Times(0);

    auto factory = make_shared<FactoryMock>();

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};

    factory.reset();
    on_tick(used_downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 1u );
}

TEST(OnTickSimple, Downloader_is_Redirect_max_redirect)
{
    const std::size_t max_redirect = 10;

    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ std::make_shared<Task>(used_task), used_downloader, max_redirect };
    Job other_job{ std::make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    const Task next_task{ string{"http://internet.org/next"}, string{"fname_next.zip"} };
    auto next_task_ptr = make_shared<Task>(next_task);
    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return(next_task_ptr) );

    auto next_downloader = make_shared<DownloaderMock>();
    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(next_task) )
            .WillOnce( Return(next_downloader) );

    OnTickSimple<JobList> on_tick{job_list, factory, task_list, max_redirect};
    on_tick(used_downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Insert next Job
    Job next_job{ next_task_ptr, next_downloader };
    it = std::find_if( it_begin, it_end,
                       [&next_job, &next_task](const auto& job) { return job == next_job && *(job.task) == next_task; } );
    ASSERT_NE( it, it_end );

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 2u );
}

TEST(OnTickSimple, Downloader_is_Redirect_Factory_returning_null)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "bad_uri";
    auto used_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    auto other_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    const Task used_task{ string{"http://internet.org/"}, string{"fname.zip"} };
    const Task other_task{ string{"http://internet.org/2"}, string{"fname2.zip"} };
    Job used_job{ make_shared<Task>(used_task), used_downloader };
    Job other_job{ make_shared<Task>(other_task), other_downloader };
    JobList job_list{used_job, other_job};

    const Task next_task{ string{"http://internet.org/next"}, string{"fname_next.zip"} };
    auto next_task_ptr = make_shared<Task>(next_task);
    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return(next_task_ptr) );

    Task first_call_factory_task, second_call_factory_task;
    auto next_downloader = make_shared<DownloaderMock>();
    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(_) )
            .WillOnce( DoAll( SaveArg<0>(&first_call_factory_task), Return(shared_ptr<Downloader>{}) ) )
            .WillOnce( DoAll( SaveArg<0>(&second_call_factory_task), Return(next_downloader) ) );

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(used_downloader);

    const Task redirect_task{ string{status.redirect_uri}, string{used_task.fname} };
    ASSERT_EQ( first_call_factory_task, redirect_task );
    ASSERT_EQ( second_call_factory_task, next_task );

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Insert next Job
    Job next_job{ next_task_ptr, next_downloader };
    it = std::find_if( it_begin, it_end,
                       [&next_job, &next_task](const auto& job) { return job == next_job && *(job.task) == next_task; } );
    ASSERT_NE( it, it_end );

    // Remove current Job
    it = std::find_if( it_begin, it_end,
                      [&used_job](const auto& job) { return job.task == used_job.task; } );
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find_if( it_begin, it_end,
                       [&other_job, &other_task](const auto& job) { return job == other_job && *(job.task) == other_task; } );
    ASSERT_NE( it, it_end );

    ASSERT_EQ( job_list.size(), 2u );
}

void invalid_Downloader(shared_ptr<Downloader> downloader)
{
    JobList job_list;

    auto factory = make_shared<FactoryMock>();
    EXPECT_CALL( *factory, create(_) )
            .Times(0);

    TaskListMock task_list;
    EXPECT_CALL( task_list, get() )
            .Times(0);

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    ASSERT_THROW( on_tick(downloader), std::runtime_error );
}

TEST(OnTickSimple, Downloader_null)
{
    {
        SCOPED_TRACE("Downloader_null");
        invalid_Downloader(nullptr);
    }
}

TEST(OnTickSimple, unknow_Downloader)
{
    {
        SCOPED_TRACE("unknow_Downloader");
        invalid_Downloader( make_shared<DownloaderMock>() );
    }
}
