#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "downloader_mock.h"
#include "factory_mock.h"
#include "task_mock.h"

#include "on_tick_simple.h"

#include <algorithm>

using ::testing::ReturnRef;
using ::testing::Return;
using ::testing::AtLeast;
using ::testing::_;

TEST(OnTickSimple, Downloader_is_OnTheGo)
{
    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = StatusDownloader::State::OnTheGo;
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};
    JobList copy_job_list = job_list;

    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .Times(0);

    auto factory = std::make_shared<FactoryMock>();
    EXPECT_CALL(*factory, create(_))
            .Times(0);

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(downloader);

    ASSERT_EQ(copy_job_list, job_list);
}

void Downloader_normal_completion(StatusDownloader::State state)
{
    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = state;
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};

    auto next_task = std::make_shared<Task>( std::string{"http://internet.org/next"}, std::string{"fname_next.zip"} );
    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .WillOnce(Return(next_task));

    auto factory = std::make_shared<FactoryMock>();
    auto next_downloader = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*factory, create(*next_task))
            .WillOnce(Return(next_downloader));

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Insert next Job
    Job next_job;
    next_job.downloader = next_downloader;
    next_job.task = next_task;
    it = std::find(it_begin, it_end, next_job);
    ASSERT_NE(it, it_end);

    // Remove current Job
    it = std::find(it_begin, it_end, job);
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find(it_begin, it_end, job2);
    ASSERT_NE(it, it_end);
    ASSERT_EQ(job_list.size(), 2u);
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
    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = state;
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};

    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .Times(0);

    auto factory = std::make_shared<FactoryMock>();

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};

    factory.reset();
    on_tick(downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find(it_begin, it_end, job);
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find(it_begin, it_end, job2);
    ASSERT_NE(it, it_end);
    ASSERT_EQ(job_list.size(), 1u);
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
    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = state;
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};

    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .WillOnce(Return( std::shared_ptr<Task>{} ));

    auto factory = std::make_shared<FactoryMock>();
    EXPECT_CALL(*factory, create(_))
            .Times(0);

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find(it_begin, it_end, job);
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find(it_begin, it_end, job2);
    ASSERT_NE(it, it_end);
    ASSERT_EQ(job_list.size(), 1u);
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

TEST(OnTickSimple, Downloader_is_Redirect)
{
    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};

    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .Times(0);

    auto factory = std::make_shared<FactoryMock>();
    auto redirect_task = std::make_shared<Task>( std::string{status.redirect_uri}, std::string{job.task->fname} );
    auto redirect_downloader = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*factory, create(*redirect_task))
            .WillOnce(Return(redirect_downloader));

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};
    on_tick(downloader);

    // Update Downloader in current Job
    Job updated_job;
    updated_job.downloader = redirect_downloader;
    updated_job.task = job.task;
    updated_job.redirect_count = job.redirect_count + 1;
    it = std::find(it_begin, it_end, updated_job);
    ASSERT_NE(it, it_end);

    // Don`t touch other Job
    it = std::find(it_begin, it_end, job2);
    ASSERT_NE(it, it_end);
    ASSERT_EQ(job_list.size(), 2u);
}

TEST(OnTickSimple, Downloader_is_Redirect_Factory_is_null)
{
    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};

    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .Times(0);

    auto factory = std::make_shared<FactoryMock>();

    OnTickSimple<JobList> on_tick{job_list, factory, task_list};

    factory.reset();
    on_tick(downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Remove current Job
    it = std::find(it_begin, it_end, job);
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find(it_begin, it_end, job2);
    ASSERT_NE(it, it_end);
    ASSERT_EQ(job_list.size(), 1u);

}

TEST(OnTickSimple, Downloader_is_Redirect_max_redirect)
{
    const std::size_t max_redirect = 10;

    auto downloader = std::make_shared<DownloaderMock>();
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    EXPECT_CALL(*downloader, status())
            .Times(AtLeast(1))
            .WillRepeatedly(ReturnRef(status));
    auto downloader2 = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*downloader2, status())
            .Times(0);

    Job job;
    job.task = std::make_shared<Task>( std::string{"http://internet.org/"}, std::string{"fname.zip"} );
    job.downloader = downloader;
    job.redirect_count = max_redirect;
    Job job2;
    job2.task = std::make_shared<Task>( std::string{"http://internet.org/2"}, std::string{"fname2.zip"} );
    job2.downloader = downloader2;
    JobList job_list{job, job2};

    auto next_task = std::make_shared<Task>( std::string{"http://internet.org/next"}, std::string{"fname_next.zip"} );
    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
            .WillOnce(Return(next_task));

    auto factory = std::make_shared<FactoryMock>();
    auto next_downloader = std::make_shared<DownloaderMock>();
    EXPECT_CALL(*factory, create(*next_task))
            .WillOnce(Return(next_downloader));

    OnTickSimple<JobList> on_tick{job_list, factory, task_list, max_redirect};
    on_tick(downloader);

    JobList::const_iterator it;
    const JobList::const_iterator it_begin = std::begin(job_list);
    const JobList::const_iterator it_end = std::end(job_list);

    // Insert next Job
    Job next_job;
    next_job.downloader = next_downloader;
    next_job.task = next_task;
    it = std::find(it_begin, it_end, next_job);
    ASSERT_NE(it, it_end);

    // Remove current Job
    it = std::find(it_begin, it_end, job);
    ASSERT_EQ(it, it_end);

    // Don`t touch other Job
    it = std::find(it_begin, it_end, job2);
    ASSERT_NE(it, it_end);
    ASSERT_EQ(job_list.size(), 2u);
}

void invalid_Downloader(std::shared_ptr<Downloader> downloader)
{
    JobList job_list;

    auto factory = std::make_shared<FactoryMock>();
    EXPECT_CALL(*factory, create(_))
            .Times(0);

    TaskListMock task_list;
    EXPECT_CALL(task_list, get())
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
        invalid_Downloader( std::make_shared<DownloaderMock>() );
    }
}
