// SPDX-License-Identifier: Apache-2.0

#include <vector>

#include <gtest/gtest.h>

#include "btop_tools.hpp"

TEST(tools, readfile) {
	std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "btop_readfile_test.txt";
	{
		std::ofstream ofs(temp_file);
		ofs << "line1\nline2\nline3\n";
	}

	EXPECT_EQ(Tools::readfile(temp_file, "fallback"), "line1line2line3");
	EXPECT_EQ(Tools::readfile("/non_existent_file_path_12345", "default_val"), "default_val");

	std::filesystem::remove(temp_file);
}

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
