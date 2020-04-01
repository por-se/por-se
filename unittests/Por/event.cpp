#include "por/event/event.h"
#include "por/configuration.h"

#include "gtest/gtest.h"

namespace {
	TEST(EventTest, SynchronizedEvents) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto init1 = configuration.thread_heads().begin()->second;
		auto thread1 = init1->tid();
		configuration.create_lock(thread1, 1).commit(configuration);
		auto acq1 = configuration.acquire_lock(thread1, 1).commit(configuration);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2).commit(configuration);
		auto init2 = configuration.init_thread(thread2, thread1).commit(configuration);
		auto rel1 = configuration.release_lock(thread1, 1).commit(configuration);
		auto thread3 = por::thread_id{thread2, 1};
		auto create3 = configuration.create_thread(thread2, thread3).commit(configuration);
		configuration.init_thread(thread3, thread2).commit(configuration);
		auto acq2 = configuration.acquire_lock(thread2, 1).commit(configuration);
		auto rel2 = configuration.release_lock(thread2, 1).commit(configuration);
		auto acq3 = configuration.acquire_lock(thread3, 1).commit(configuration);
		configuration.release_lock(thread3, 1).commit(configuration);
		auto acq4 = configuration.acquire_lock(thread3, 1).commit(configuration);
		auto rel4 = configuration.release_lock(thread3, 1).commit(configuration);
		auto exit2 = configuration.exit_thread(thread2).commit(configuration);
		auto join2 = configuration.join_thread(thread1, thread2).commit(configuration);

		ASSERT_TRUE(acq1->synchronized_events().empty());
		ASSERT_TRUE(exit2->synchronized_events().empty());
		ASSERT_TRUE(acq4->synchronized_events().empty());
		ASSERT_TRUE(rel4->synchronized_events().empty());

		auto acq2se = acq2->synchronized_events();
		ASSERT_EQ(acq2se.size(), static_cast<std::size_t>(1));
		ASSERT_EQ(*acq2se.begin(), rel1);

		auto join2se = join2->synchronized_events();
		{
			std::vector<por::event::event const*> expect{init2, acq2, rel2, create3, exit2};
			std::sort(expect.begin(), expect.end());
			std::vector<por::event::event const*> actual(join2se.begin(), join2se.end());
			std::sort(actual.begin(), actual.end());
			ASSERT_EQ(expect.size(), actual.size());
			for(std::size_t i = 0; i < expect.size(); ++i) {
				ASSERT_EQ(expect[i], actual[i]);
			}
		}

		auto acq3se = acq3->synchronized_events();
		{
			std::vector<por::event::event const*> expect{rel1, acq2, rel2};
			std::sort(expect.begin(), expect.end());
			std::vector<por::event::event const*> actual(acq3se.begin(), acq3se.end());
			std::sort(actual.begin(), actual.end());
			ASSERT_EQ(expect.size(), actual.size());
			for(std::size_t i = 0; i < expect.size(); ++i) {
				ASSERT_EQ(expect[i], actual[i]) << expect[i]->to_string(true) << " != " << actual[i]->to_string(true);
			}
		}
	}
} // namespace
