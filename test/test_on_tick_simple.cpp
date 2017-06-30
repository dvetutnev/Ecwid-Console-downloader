#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mock/downloader_mock.h"
#include "mock/factory_mock.h"
#include "mock/task_mock.h"
#include "mock/dashboard_mock.h"

#include "on_tick_simple.h"

#include <algorithm>
#include <random>

using ::std::size_t;
using ::std::string;
using ::std::shared_ptr;
using ::std::make_shared;
using ::std::make_unique;
using ::std::move;
using ::std::begin;
using ::std::end;
using ::std::find_if;

using ::testing::ReturnRef;
using ::testing::Return;
using ::testing::ByMove;
using ::testing::Invoke;
using ::testing::AtLeast;
using ::testing::_;
using ::testing::DoAll;

using JobList = std::list<Job>;

struct OnTickSimpleF : public ::testing::Test
{
    OnTickSimpleF()
        : used_fname{"fname.zip"},
          used_job{used_fname},
          used_job_id{used_job.id},

          other_fname{"fname2.zip"},
          other_job{other_fname},
          other_job_id{other_job.id},

          used_downloader{ make_shared<DownloaderMock>() },
          other_downloader{ make_shared<DownloaderMock>() },
          factory{ make_shared<FactoryMock>() }
    {
        used_job.downloader = used_downloader;
        other_job.downloader = other_downloader;

        job_list.push_back( move(used_job) );
        job_list.push_back( move(other_job) );
    }


    const string used_fname;
    Job used_job;
    const size_t used_job_id;

    const string other_fname;
    Job other_job;
    const size_t other_job_id;

    shared_ptr<DownloaderMock> used_downloader;
    shared_ptr<DownloaderMock> other_downloader;

    shared_ptr<FactoryMock> factory;
    TaskListMock task_list;
    DashboardMock dashboard;

    JobList job_list;
};

TEST_F(OnTickSimpleF, Downloader_is_OnTheGo)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::OnTheGo;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    EXPECT_CALL( *factory, create(_,_,_) )
            .Times(0);

    EXPECT_CALL( task_list, get() )
            .Times(0);

    EXPECT_CALL( dashboard, update(used_job_id,_) )
            .Times(1);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);

    auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto next_it = find_if( begin(job_list), end(job_list), used_predicate );
    ASSERT_NE( next_it, end(job_list) );

    auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
    ASSERT_NE( other_it, end(job_list) );

    ASSERT_EQ( job_list.size(), 2u );
}

struct OnTickSimpleF_nextTask : public OnTickSimpleF
{
    OnTickSimpleF_nextTask()
        : next_uri{"http://internet.org/next"},
          next_fname{"fname_next.zip"},

          next_downloader{ make_shared<DownloaderMock>() }
    {}

    virtual void SetUp()
    {
        EXPECT_CALL( task_list, get() )
                .WillOnce( Return( ByMove( make_unique<Task>(next_uri, next_fname) ) ) );

        EXPECT_CALL( *factory, create(_, next_uri, next_fname) )
                .WillOnce( Return(next_downloader) );

        EXPECT_CALL( dashboard, update(used_job_id,_) )
                .Times(1);
    }

    virtual void TearDown()
    {
        // Insert next Job
        auto next_predicate = [downloader = next_downloader, fname = next_fname](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname;
        };
        auto next_it = find_if( begin(job_list), end(job_list), next_predicate );
        ASSERT_NE( next_it, end(job_list) );

        // Remove current Job
        auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
        ASSERT_EQ( used_it, end(job_list) );

        // Don`t touch other Job
        auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
        ASSERT_NE( other_it, end(job_list) );

        ASSERT_EQ( job_list.size(), 2u );
    }

    const string next_uri;
    const string next_fname;

    shared_ptr<DownloaderMock> next_downloader;
};

TEST_F(OnTickSimpleF_nextTask, Downloader_is_Done)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Done;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);
}

TEST_F(OnTickSimpleF_nextTask, Downloader_is_Failed)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Failed;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);
}

struct OnTickSimpleF_skipBadTask : public OnTickSimpleF
{
    OnTickSimpleF_skipBadTask()
        : bad_uri{"bad_uri"},
          bad_fname{"bad_fname"},

          next_uri{"http://internet.org/next"},
          next_fname{"fname_next.zip"},

          next_downloader{ make_shared<DownloaderMock>() }
    {}

    virtual void SetUp()
    {
        EXPECT_CALL( task_list, get() )
                .WillOnce( Return( ByMove( make_unique<Task>(bad_uri, bad_fname) ) ) )
                .WillOnce( Return( ByMove( make_unique<Task>(next_uri, next_fname) ) ) );

        EXPECT_CALL( *factory, create(_,_,_) )
                .WillOnce( DoAll( Invoke( [this](const size_t, const string& uri, const string& fname) { ASSERT_EQ( uri, bad_uri ); ASSERT_EQ( fname, bad_fname );  } ),
                                  Return(nullptr) ) )
                .WillOnce( DoAll( Invoke( [this](const size_t, const string& uri, const string& fname) { ASSERT_EQ( uri, next_uri ); ASSERT_EQ( fname, next_fname );  } ),
                                  Return(next_downloader) ) );

        EXPECT_CALL( dashboard, update(used_job_id,_) )
                .Times(1);
    }

    virtual void TearDown()
    {
        // Insert next Job
        auto next_predicate = [downloader = next_downloader, fname = next_fname](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname;
        };
        auto next_it = find_if( begin(job_list), end(job_list), next_predicate );
        ASSERT_NE( next_it, end(job_list) );

        // Remove current Job
        auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
        ASSERT_EQ( used_it, end(job_list) );

        // Don`t touch other Job
        auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
        ASSERT_NE( other_it, end(job_list) );

        ASSERT_EQ( job_list.size(), 2u );
    }

    const string bad_uri;
    const string bad_fname;

    const string next_uri;
    const string next_fname;

    shared_ptr<DownloaderMock> next_downloader;
};

TEST_F(OnTickSimpleF_skipBadTask, Downloader_is_Done)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Done;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);
}

TEST_F(OnTickSimpleF_skipBadTask, Downloader_is_Failed)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Failed;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);
}

struct OnTickSimpleF_factoryNull : public OnTickSimpleF
{
    virtual void SetUp()
    {
        EXPECT_CALL( task_list, get() )
                .Times(0);

        EXPECT_CALL( *factory, create(_,_,_) )
                .Times(0);

        EXPECT_CALL( dashboard, update(used_job_id,_) )
                .Times(1);
    }

    virtual void TearDown()
    {
        // Remove current Job
        auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
        ASSERT_EQ( used_it, end(job_list) );

        // Don`t touch other Job
        auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
        ASSERT_NE( other_it, end(job_list) );

        ASSERT_EQ( job_list.size(), 1u );
    }
};

TEST_F(OnTickSimpleF_factoryNull, Downloader_is_Done)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Done;
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);


    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    factory.reset();
    on_tick.invoke(used_downloader);
}

TEST_F(OnTickSimpleF_factoryNull, Downloader_is_Failed)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Done;
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);


    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    factory.reset();
    on_tick.invoke(used_downloader);
}

struct OnTickSimpleF_noTask : public OnTickSimpleF
{
    virtual void SetUp()
    {
        EXPECT_CALL( task_list, get() )
                .WillOnce( Return( ByMove(nullptr) ) );

        EXPECT_CALL( *factory, create(_,_,_) )
                .Times(0);

        EXPECT_CALL( dashboard, update(used_job_id,_) )
                .Times(1);
    }

    virtual void TearDown()
    {
        // Remove current Job
        auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
        ASSERT_EQ( used_it, end(job_list) );

        // Don`t touch other Job
        auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
        {
            return job.downloader == downloader && job.fname == fname && job.id == id;
        };
        auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
        ASSERT_NE( other_it, end(job_list) );

        ASSERT_EQ( job_list.size(), 1u );
    }
};

TEST_F(OnTickSimpleF_noTask, Downloader_is_Done)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Done;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);
}

TEST_F(OnTickSimpleF_noTask, Downloader_is_Failed)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Failed;
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);
}

TEST_F(OnTickSimpleF, Downloader_is_Redirect)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    auto redirect_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *factory, create(used_job_id, status.redirect_uri, used_fname) )
            .WillOnce( Return(redirect_downloader) );

    EXPECT_CALL( task_list, get() )
            .Times(0);

    EXPECT_CALL( dashboard, update(used_job_id,_) )
            .Times(1);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);

    // Update used Job
    auto used_predicate = [downloader = redirect_downloader, fname = used_fname, id = used_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
    ASSERT_NE( used_it, end(job_list) );
    ASSERT_EQ( used_it->redirect_count, 1u );

    // Don`t touch other Job
    auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
    ASSERT_NE( other_it, end(job_list) );

    ASSERT_EQ( job_list.size(), 2u );
}

TEST_F(OnTickSimpleF, Downloader_is_Redirect_Factory_is_null)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "http://internet.org/redirect";
    EXPECT_CALL( *used_downloader, status() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    EXPECT_CALL( *factory, create(_,_,_) )
            .Times(0);

    EXPECT_CALL( task_list, get() )
            .Times(0);

    EXPECT_CALL( dashboard, update(_,_) )
            .Times(1);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};

    factory.reset();
    on_tick.invoke(used_downloader);

    // Remove current Job
    auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
    ASSERT_EQ( used_it, end(job_list) );

    // Don`t touch other Job
    auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
    ASSERT_NE( other_it, end(job_list) );

    ASSERT_EQ( job_list.size(), 1u );
}

TEST_F(OnTickSimpleF, Downloader_is_Redirect_max_redirect)
{
    const size_t max_redirect = 2;

    StatusDownloader status_1;
    status_1.state = StatusDownloader::State::Redirect;
    status_1.redirect_uri = "http://internet.org/redirect_1";
    EXPECT_CALL( *used_downloader, status() )
            .WillRepeatedly( ReturnRef(status_1) );

    StatusDownloader status_2;
    status_2.state = StatusDownloader::State::Redirect;
    status_2.redirect_uri = "http://internet.org/redirect_2";
    auto first_redirect_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *first_redirect_downloader, status() )
            .WillRepeatedly( ReturnRef(status_2) );

    StatusDownloader status_3;
    status_3.state = StatusDownloader::State::Redirect;
    status_3.redirect_uri = "http://internet.org/redirect_3";
    auto second_redirect_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *second_redirect_downloader, status() )
            .WillRepeatedly( ReturnRef(status_3) );

    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    auto next_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *factory, create(_,_,_) )
            .WillOnce( Return(first_redirect_downloader) )
            .WillOnce( Return(second_redirect_downloader) )
            .WillOnce( Return(next_downloader) );

    const string next_uri = "http://internet.org/next";
    const string next_fname = "fname_next.zip";
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return( ByMove( make_unique<Task>(next_uri, next_fname) ) ) );

    EXPECT_CALL( dashboard, update(used_job_id, _) )
            .Times(2);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard, max_redirect};

    on_tick.invoke(used_downloader);
    on_tick.invoke(first_redirect_downloader);

    EXPECT_CALL( dashboard, update(used_job_id, _) )
            .WillOnce( Return() )
            .WillOnce( Invoke( [](size_t, const StatusDownloader& status) { ASSERT_EQ( status.state, StatusDownloader::State::Failed); } ) );

    on_tick.invoke(second_redirect_downloader);

    // Insert next Job
    auto next_predicate = [downloader = next_downloader, fname = next_fname](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname;
    };
    auto next_it = find_if( begin(job_list), end(job_list), next_predicate );
    ASSERT_NE( next_it, end(job_list) );

    // Remove current Job
    auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
    ASSERT_EQ( used_it, end(job_list) );

    // Don`t touch other Job
    auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
    ASSERT_NE( other_it, end(job_list) );

    ASSERT_EQ( job_list.size(), 2u );
}

TEST_F(OnTickSimpleF, Downloader_is_Redirect_Factory_returning_null)
{
    StatusDownloader status;
    status.state = StatusDownloader::State::Redirect;
    status.redirect_uri = "bad_uri";
    EXPECT_CALL( *used_downloader, status() )
            .Times(1)
            .WillRepeatedly( ReturnRef(status) );
    EXPECT_CALL( *other_downloader, status() )
            .Times(0);

    auto next_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *factory, create(_,_,_) )
            .WillOnce( Return(nullptr) )
            .WillOnce( Return(next_downloader) );

    const string next_uri = "http://internet.org/next";
    const string next_fname = "fname_next.zip";
    EXPECT_CALL( task_list, get() )
            .WillOnce( Return( ByMove( make_unique<Task>(next_uri, next_fname) ) ) );

    EXPECT_CALL( dashboard, update(_,_) )
            .Times(1);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    on_tick.invoke(used_downloader);

    // Insert next Job
    auto next_predicate = [downloader = next_downloader, fname = next_fname](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname;
    };
    auto next_it = find_if( begin(job_list), end(job_list), next_predicate );
    ASSERT_NE( next_it, end(job_list) );

    // Remove current Job
    auto used_predicate = [downloader = used_downloader, fname = used_fname, id = used_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto used_it = find_if( begin(job_list), end(job_list), used_predicate );
    ASSERT_EQ( used_it, end(job_list) );

    // Don`t touch other Job
    auto other_predicate = [downloader = other_downloader, fname = other_fname, id = other_job_id](const auto& job)
    {
        return job.downloader == downloader && job.fname == fname && job.id == id;
    };
    auto other_it = find_if( begin(job_list), end(job_list), other_predicate );
    ASSERT_NE( other_it, end(job_list) );

    ASSERT_EQ( job_list.size(), 2u );
}

struct OnTickSimpleF_invalidDownloader : public OnTickSimpleF
{
    OnTickSimpleF_invalidDownloader()
    {
        EXPECT_CALL( *used_downloader, status() )
                .Times(0);
        EXPECT_CALL( *other_downloader, status() )
                .Times(0);
        EXPECT_CALL( *factory, create(_,_,_) )
                .Times(0);
        EXPECT_CALL( task_list, get() )
                .Times(0);
        EXPECT_CALL( dashboard, update(_,_) )
                .Times(0);
    }
};

TEST_F(OnTickSimpleF_invalidDownloader, null_ptr)
{
    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    ASSERT_THROW( on_tick.invoke(nullptr), std::runtime_error );
}

TEST_F(OnTickSimpleF_invalidDownloader, unknow_Downloader)
{
    auto unknow_downloader = make_shared<DownloaderMock>();
    EXPECT_CALL( *unknow_downloader, status() )
            .Times(0);

    OnTickSimple on_tick{job_list, factory, task_list, dashboard};
    ASSERT_THROW( on_tick.invoke(unknow_downloader), std::runtime_error );
}
