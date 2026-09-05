// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "btop_config.hpp"
#include "btop_shared.hpp"
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

TEST(tools, s_replace) {
	EXPECT_EQ(Tools::s_replace("hello world", "world", "there"), "hello there");
	EXPECT_EQ(Tools::s_replace("foo bar foo baz foo", "foo", "qux"), "qux bar qux baz qux");
	EXPECT_EQ(Tools::s_replace("foo bar baz", " ", "_"), "foo_bar_baz");
	EXPECT_EQ(Tools::s_replace("aaa", "a", "bb"), "bbbbbb");
	EXPECT_EQ(Tools::s_replace("aaa", "a", "aa"), "aaaaaa");
	EXPECT_EQ(Tools::s_replace("aba", "a", "aba"), "abababa");
	EXPECT_EQ(Tools::s_replace("hello", "l", ""), "heo");
	EXPECT_EQ(Tools::s_replace("abc", "d", "e"), "abc");
	EXPECT_EQ(Tools::s_replace("hello", "xyz", "abc"), "hello");
	EXPECT_EQ(Tools::s_replace("hello", "", "world"), "hello");
	EXPECT_EQ(Tools::s_replace("", "a", "b"), "");
}

TEST(cpu, trim_name) {
	EXPECT_EQ(Cpu::trim_name("Intel(R) Xeon(R) CPU E5-2670 v3 @ 2.30GHz"), "E5-2670");
	EXPECT_EQ(Cpu::trim_name("Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz"), "i7-10700K");
	EXPECT_EQ(Cpu::trim_name("AMD Ryzen 9 5900X 12-Core Processor"), "Ryzen 9 5900X");
	EXPECT_EQ(Cpu::trim_name("AMD Ryzen AI 9 HX 370 Processor"), "Ryzen AI 9 HX 370");
	EXPECT_EQ(Cpu::trim_name("Apple M1 Max"), "M1 Max");
	EXPECT_EQ(Cpu::trim_name(""), "");
}

class EnvGuard {
	std::string key;
	std::optional<std::string> old_value;
public:
	explicit EnvGuard(const char* env_key) : key(env_key) {
		if (const char* val = std::getenv(env_key)) {
			old_value = val;
		}
	}
	~EnvGuard() {
		if (old_value.has_value()) {
			setenv(key.c_str(), old_value.value().c_str(), 1);
		} else {
			unsetenv(key.c_str());
		}
	}
};

TEST(config, get_config_dir_valid) {
	EnvGuard xdg_guard("XDG_CONFIG_HOME");
	std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "btop_test_config_valid";
	std::filesystem::remove_all(temp_dir);
	std::filesystem::create_directories(temp_dir);

	setenv("XDG_CONFIG_HOME", temp_dir.c_str(), 1);

	auto res = Config::get_config_dir();
	ASSERT_TRUE(res.has_value());
	EXPECT_EQ(res.value(), temp_dir / "btop");
	EXPECT_TRUE(std::filesystem::is_directory(temp_dir / "btop"));

	std::filesystem::remove_all(temp_dir);
}

TEST(config, get_config_dir_read_only) {
	EnvGuard xdg_guard("XDG_CONFIG_HOME");
	std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "btop_test_config_ro";
	std::filesystem::path btop_dir = temp_dir / "btop";
	std::filesystem::remove_all(temp_dir);
	std::filesystem::create_directories(btop_dir);

	setenv("XDG_CONFIG_HOME", temp_dir.c_str(), 1);

	// Remove write permissions on btop_dir
	std::filesystem::permissions(btop_dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec, std::filesystem::perm_options::replace);

	auto res = Config::get_config_dir();
	ASSERT_TRUE(res.has_value());
	EXPECT_EQ(res.value(), btop_dir);

	// Restore write permissions so cleanup succeeds
	std::filesystem::permissions(btop_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
	std::filesystem::remove_all(temp_dir);
}

TEST(config, get_config_dir_not_a_directory) {
	EnvGuard xdg_guard("XDG_CONFIG_HOME");
	std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "btop_test_config_file";
	std::filesystem::remove_all(temp_dir);
	std::filesystem::create_directories(temp_dir);

	std::filesystem::path file_path = temp_dir / "btop";
	{
		std::ofstream ofs(file_path);
		ofs << "not a directory";
	}

	setenv("XDG_CONFIG_HOME", temp_dir.c_str(), 1);

	auto res = Config::get_config_dir();
	EXPECT_FALSE(res.has_value());

	std::filesystem::remove_all(temp_dir);
}

TEST(config, get_config_dir_unreadable) {
	if (getuid() == 0) {
		// Root bypasses read permission checks on Linux filesystems, skip this test if running as root
		GTEST_SKIP() << "Skipping test as root user ignores standard read permission checks";
	}
	EnvGuard xdg_guard("XDG_CONFIG_HOME");
	std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "btop_test_config_unreadable";
	std::filesystem::path btop_dir = temp_dir / "btop";
	std::filesystem::remove_all(temp_dir);
	std::filesystem::create_directories(btop_dir);

	setenv("XDG_CONFIG_HOME", temp_dir.c_str(), 1);

	std::filesystem::permissions(btop_dir, std::filesystem::perms::none, std::filesystem::perm_options::replace);

	auto res = Config::get_config_dir();
	EXPECT_FALSE(res.has_value());

	std::filesystem::permissions(btop_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
	std::filesystem::remove_all(temp_dir);
}
