// SPDX-License-Identifier: Apache-2.0

#include "btop.hpp"

#include <iterator>
#include <ranges>
#include <string_view>
#include <vector>

auto main(int argc, const char* argv[]) -> int {
	auto args_view = std::views::counted(std::next(argv), argc - 1);
	std::vector<std::string_view> args;
	for (auto&& arg : args_view) {
		args.emplace_back(arg);
	}
	return btop_main(args);
}
