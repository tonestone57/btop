// SPDX-License-Identifier: Apache-2.0

#include <vector>

#include <gtest/gtest.h>

#include "btop_shared.hpp"
#include "btop_tools.hpp"

TEST(tools, string_split) {
	EXPECT_EQ(Tools::ssplit(""), std::vector<std::string> {});
	EXPECT_EQ(Tools::ssplit("foo"), std::vector<std::string> { "foo" });
	{
		auto actual = Tools::ssplit("foo       bar         baz    ");
		auto expected = std::vector<std::string> { "foo", "bar", "baz" };
		EXPECT_EQ(actual, expected);
	}

	{
		auto actual = Tools::ssplit("foobo  oho  barbo  bo  bazbo", 'o');
		auto expected = std::vector<std::string> { "f", "b", "  ", "h", "  barb", "  b", "  bazb" };
		EXPECT_EQ(actual, expected);
	}
}

TEST(cpu, trim_name) {
	EXPECT_EQ(Cpu::trim_name("Intel(R) Xeon(R) CPU E5-2670 v3 @ 2.30GHz"), "E5-2670");
	EXPECT_EQ(Cpu::trim_name("Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz"), "i7-10700K");
	EXPECT_EQ(Cpu::trim_name("AMD Ryzen 9 5900X 12-Core Processor"), "Ryzen 9 5900X");
	EXPECT_EQ(Cpu::trim_name("AMD Ryzen AI 9 HX 370 Processor"), "Ryzen AI 9 HX 370");
	EXPECT_EQ(Cpu::trim_name("Apple M1 Max"), "M1 Max");
	EXPECT_EQ(Cpu::trim_name(""), "");
}
