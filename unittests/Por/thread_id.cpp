#include "por/thread_id.h"

#include "gtest/gtest.h"

#include <iostream>

using namespace por;

namespace {
//
// Basic functions
//
TEST(ThreadIdTest, BasicFunctions) {
	thread_id empty{};
	thread_id singleLayer(thread_id(), 1);
	thread_id doubleLayer(singleLayer, 2);

	ASSERT_TRUE(empty.empty() && empty.size() == 0);
	ASSERT_TRUE(!singleLayer.empty() && singleLayer.size() == 1);
	ASSERT_TRUE(!doubleLayer.empty() && doubleLayer.size() == 2);

	ASSERT_EQ(singleLayer.ids()[0], 1);
	ASSERT_TRUE(doubleLayer.ids()[0] == 1 && doubleLayer.ids()[1] == 2);
}

//
// Operator overloads
//
TEST(ThreadIdTest, OperatorOverloads) {
	ASSERT_TRUE(thread_id(thread_id(), 1));
	ASSERT_TRUE(!thread_id());

	ASSERT_EQ(thread_id(thread_id(), 1)[0], 1);

	ASSERT_EQ(thread_id(thread_id(thread_id(), 1), 2)[1], 2);

	// Special test that goes deeper into the hierarchy
	thread_id tid(thread_id(), 1);
	for(int i = 0; i < 12; i++) {
		tid = thread_id(tid, i + 2);

		for(int j = 0; j <= i + 1; j++) {
			ASSERT_EQ(tid[j], j + 1);
		}
	}
}

//
// Formatting of thread ids
//
TEST(ThreadIdTest, Formatting) {
	ASSERT_EQ(thread_id(thread_id(), 1).to_string(), "1");
	ASSERT_EQ(thread_id(thread_id(thread_id(), 1), 1).to_string(), "1,1");
	ASSERT_EQ(thread_id(thread_id(thread_id(), 1), 10000).to_string(), "1,10000");

	// special case
	ASSERT_EQ(thread_id().to_string(), "");
}

//
// Parsing of thread ids
//
static void testParsing(std::string input, bool shouldWork) {
	auto result = thread_id::from_string(input);

	if(static_cast<bool>(result) != shouldWork) {
		std::cerr << "Parsing of input: '" << input << "' should not have been successful.\n";
		exit(-1);
	}

	if(shouldWork) {
		std::string output = result->to_string();

		if(output != input) {
			std::cerr << "Parsing tid does not match input input: '" << input << "' output: '" << output << "'.\n";
			exit(-1);
		}
	}
}

static void testParsingOfThreadIds() {
	testParsing("1", true);
	testParsing("1,2,3", true);
	testParsing("1231,12312,4334", true);
	testParsing("1,1,1,1,1,1,1,1,1,1", true);
	testParsing("9,8,7,6,5,4,3,2,1", true);

	// Simply in the wrong format
	testParsing("", false);
	testParsing("1,", false);
	testParsing(",1", false);
	testParsing("1 1", false);
	testParsing(" 1,1", false);
	testParsing("1,,1", false);
	testParsing("1.1", false);
	testParsing("a", false);
	testParsing("1,1,1,1,1,1,1,1,1,1,1,1,1,1,a,1", false);
	testParsing("1,\n1", false);
	testParsing("1,\t1", false);

	// Invalid local ids
	testParsing("1,1,1,1,0,1", false); // 0 is not allowed
	testParsing("1,123123121", false); // simple overflow

	exit(0);
}

TEST(ThreadIdDeathTest, Parsing) {
	ASSERT_EXIT(testParsingOfThreadIds(), ::testing::ExitedWithCode(0), "");
}
} // namespace
