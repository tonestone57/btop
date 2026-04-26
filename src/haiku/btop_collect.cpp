/* Copyright 2021 Aristocratos (jakob@qvantnet.com)
   Haiku port by Jules (AI Assistant)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

#include <OS.h>
#include <fs_info.h>
#include <unistd.h>
#include <pwd.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <sys/statvfs.h>
#include <sys/time.h>

#include "../btop_config.hpp"
#include "../btop_log.hpp"
#include "../btop_shared.hpp"
#include "../btop_tools.hpp"

using std::clamp, std::string_literals::operator""s, std::cmp_equal, std::cmp_less, std::cmp_greater;
using std::ifstream, std::numeric_limits, std::streamsize, std::round, std::max, std::min, std::to_string;
namespace fs = std::filesystem;
using namespace Tools;

//? --------------------------------------------------- FUNCTIONS -----------------------------------------------------

namespace Cpu {
	string box;
	int x, y, width, height, min_width, min_height;
	bool shown, redraw, got_sensors = false, cpu_temp_only = false, has_battery = false, supports_watts = false;
	vector<long long> core_old_active;
	vector<long long> core_old_total;
	cpu_info current_cpu;
	string cpuName, cpuHz;
	vector<string> available_fields = {"total"};
	vector<string> available_sensors = {"Auto"};
	std::unordered_map<int, int> core_mapping;
	tuple<int, float, long, string> current_bat;
	std::optional<std::string> container_engine;

	string get_cpuName() {
		system_info info;
		if (get_system_info(&info) == B_OK) {
			return info.cpu_type != 0 ? "Haiku CPU" : "Unknown";
		}
		return "Unknown";
	}

	auto collect(bool no_update) -> cpu_info& {
		if (Runner::stopping or (no_update and not current_cpu.cpu_percent.at("total").empty()))
			return current_cpu;

		system_info info;
		if (get_system_info(&info) != B_OK) return current_cpu;

		if (getloadavg(current_cpu.load_avg.data(), 3) < 0) {
			current_cpu.load_avg.fill(0.0);
		}

		std::vector<cpu_info_t> cpu_infos(Shared::coreCount);
		if (get_cpu_info(0, Shared::coreCount, cpu_infos.data()) == B_OK) {
			long long total_active = 0;
			long long total_total = system_time() * Shared::coreCount;

			for (int i = 0; i < Shared::coreCount; i++) {
				long long active = cpu_infos[i].active_time;
				long long total = system_time();
				long long diff_active = active - core_old_active.at(i);
				long long diff_total = total - core_old_total.at(i);

				core_old_active.at(i) = active;
				core_old_total.at(i) = total;

				if (diff_total > 0) {
					current_cpu.core_percent.at(i).push_back(clamp((long long)round((double)diff_active * 100 / diff_total), 0ll, 100ll));
				} else {
					current_cpu.core_percent.at(i).push_back(0);
				}
				if (current_cpu.core_percent.at(i).size() > 40) current_cpu.core_percent.at(i).pop_front();
				total_active += active;
			}

			static long long old_total_active = 0;
			static long long old_total_total = 0;
			long long diff_total_active = total_active - old_total_active;
			long long diff_total_total = total_total - old_total_total;
			old_total_active = total_active;
			old_total_total = total_total;

			if (diff_total_total > 0) {
				current_cpu.cpu_percent.at("total").push_back(clamp((long long)round((double)diff_total_active * 100 / diff_total_total), 0ll, 100ll));
			} else {
				current_cpu.cpu_percent.at("total").push_back(0);
			}
		}

		while (current_cpu.cpu_percent.at("total").size() > width * 2) {
			for (auto& [name, deque] : current_cpu.cpu_percent) {
				if (!deque.empty()) deque.pop_front();
			}
		}

		return current_cpu;
	}

	auto get_core_mapping() -> std::unordered_map<int, int> {
		std::unordered_map<int, int> core_map;
		for (int i = 0; i < Shared::coreCount; i++) core_map[i] = i;
		return core_map;
	}

	auto get_cpuHz() -> string {
		system_info info;
		if (get_system_info(&info) == B_OK) {
			return to_string(info.cpu_clock_speed / 1000000);
		}
		return "";
	}

	auto get_battery() -> tuple<int, float, long, string> {
		return {0, 0.0, 0, ""};
	}
}

namespace Mem {
	string box;
	int x, y, width, height, min_width, min_height;
	bool has_swap = false, shown, redraw;
	mem_info current_mem;
	int disk_ios = 0;

	uint64_t get_totalMem() {
		system_info info;
		if (get_system_info(&info) == B_OK) return (uint64_t)info.max_pages * B_PAGE_SIZE;
		return 0;
	}

	auto collect(bool no_update) -> mem_info& {
		if (Runner::stopping or (no_update and not current_mem.percent.at("used").empty()))
			return current_mem;

		system_info info;
		if (get_system_info(&info) == B_OK) {
			current_mem.stats["used"] = (uint64_t)info.used_pages * B_PAGE_SIZE;
			current_mem.stats["available"] = (uint64_t)(info.max_pages - info.used_pages) * B_PAGE_SIZE;
			current_mem.stats["cached"] = (uint64_t)info.cached_pages * B_PAGE_SIZE;
			current_mem.stats["free"] = (uint64_t)info.free_pages * B_PAGE_SIZE;
		}

		for (const auto& name : mem_names) {
			current_mem.percent[name].push_back(round((double)current_mem.stats[name] * 100 / get_totalMem()));
			while (current_mem.percent[name].size() > width * 2) current_mem.percent[name].pop_front();
		}

		if (Config::getB("show_disks")) {
			current_mem.disks.clear();
			current_mem.disks_order.clear();
			int32 cookie = 0;
			dev_t dev;
			while ((dev = next_dev(&cookie)) >= 0) {
				fs_info fsi;
				if (fs_stat_dev(dev, &fsi) == B_OK) {
					disk_info di;
					di.dev = fsi.device_name;
					di.name = fsi.volume_name[0] == '\0' ? fsi.device_name : fsi.volume_name;
					di.total = (int64_t)fsi.total_blocks * fsi.block_size;
					di.free = (int64_t)fsi.free_blocks * fsi.block_size;
					di.used = di.total - di.free;
					if (di.total > 0) {
						di.used_percent = round((double)di.used * 100 / di.total);
						di.free_percent = 100 - di.used_percent;
					}
					current_mem.disks[di.name] = di;
					current_mem.disks_order.push_back(di.name);
				}
			}
		}

		return current_mem;
	}
}

namespace Net {
	string box;
	int x, y, width, height, min_width, min_height;
	bool shown, redraw;
	std::unordered_map<string, net_info> current_net;
	net_info empty_net = {};
	vector<string> interfaces;
	string selected_iface;
	std::unordered_map<string, uint64_t> graph_max = {{"download", {}}, {"upload", {}}};
	bool rescale = true;
	uint64_t timestamp = 0;

	auto collect(bool no_update) -> net_info& {
		if (Runner::stopping) return current_net.contains(selected_iface) ? current_net.at(selected_iface) : empty_net;
		auto& net = current_net;
		auto new_timestamp = time_ms();

		if (!no_update) {
			IfAddrsPtr if_addrs{};
			if (if_addrs.get_status() != 0) return empty_net;

			interfaces.clear();
			for (auto* ifa = if_addrs.get(); ifa != nullptr; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr == nullptr) continue;
				const auto& iface = ifa->ifa_name;
				if (!v_contains(interfaces, iface)) {
					interfaces.push_back(iface);
					net[iface].connected = (ifa->ifa_flags & IFF_UP);
				}

				char ip[INET6_ADDRSTRLEN];
				if (ifa->ifa_addr->sa_family == AF_INET) {
					inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ip, INET_ADDRSTRLEN);
					net[iface].ipv4 = ip;
				} else if (ifa->ifa_addr->sa_family == AF_INET6) {
					inet_ntop(AF_INET6, &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr, ip, INET6_ADDRSTRLEN);
					net[iface].ipv6 = ip;
				}
			}
			timestamp = new_timestamp;
		}

		if (selected_iface.empty() || !v_contains(interfaces, selected_iface)) {
			if (!interfaces.empty()) selected_iface = interfaces.at(0);
			else return empty_net;
		}

		return net.at(selected_iface);
	}
}

namespace Proc {
	atomic<int> numpids = 0;
	string box;
	int x, y, width, height, min_width, min_height;
	bool shown, redraw;
	int select_max;
	atomic<int> detailed_pid;
	int selected_pid, start, selected, collapse = -1, expand = -1, filter_found = 0, selected_depth, toggle_children = -1;
	int scroll_pos;
	string selected_name;
	atomic<bool> resized;

	vector<proc_info> current_procs;
	std::unordered_map<string, string> uid_user;
	string current_sort;
	string current_filter;
	bool current_rev = false;
	bool is_tree_mode;
	detail_container detailed;

	struct proc_times {
		bigtime_t user_time;
		bigtime_t kernel_time;
		bigtime_t timestamp;
	};
	std::unordered_map<size_t, proc_times> old_proc_times;

	auto collect(bool no_update) -> vector<proc_info>& {
		if (Runner::stopping or (no_update and not current_procs.empty()))
			return current_procs;

		current_procs.clear();
		int32 team_cookie = 0;
		team_info ti;
		bigtime_t now = system_time();
		while (get_next_team_info(&team_cookie, &ti) == B_OK) {
			proc_info pi;
			pi.pid = ti.team;
			pi.ppid = 0;
			pi.name = fs::path(ti.args).filename();
			pi.cmd = ti.args;
			pi.state = 'R';

			struct passwd* pwd = getpwuid(ti.uid);
			if (pwd) pi.user = pwd->pw_name;
			else pi.user = to_string(ti.uid);

			int32 thread_cookie = 0;
			thread_info thi;
			pi.threads = 0;
			bigtime_t team_user_time = 0;
			bigtime_t team_kernel_time = 0;
			while (get_next_thread_info(ti.team, &thread_cookie, &thi) == B_OK) {
				pi.threads++;
				team_user_time += thi.user_time;
				team_kernel_time += thi.kernel_time;
			}

			if (old_proc_times.contains(pi.pid)) {
				auto& old = old_proc_times.at(pi.pid);
				bigtime_t diff_time = (team_user_time + team_kernel_time) - (old.user_time + old.kernel_time);
				bigtime_t diff_period = now - old.timestamp;
				if (diff_period > 0) {
					pi.cpu_p = clamp((double)diff_time * 100 / diff_period, 0.0, 100.0 * Shared::coreCount);
				}
			}
			old_proc_times[pi.pid] = {team_user_time, team_kernel_time, now};

			pi.mem = (uint64_t)ti.used_bss + ti.used_data + ti.used_text;
			current_procs.push_back(pi);
		}

		numpids = current_procs.size();
		return current_procs;
	}
}

namespace Shared {
	long coreCount, page_size, clk_tck;

	void init() {
		system_info info;
		if (get_system_info(&info) == B_OK) {
			coreCount = info.cpu_count;
			page_size = B_PAGE_SIZE;
		} else {
			coreCount = 1;
			page_size = 4096;
		}
		clk_tck = 1000000;

		// Initialize Cpu maps
		Cpu::current_cpu.cpu_percent["total"] = {};

		Cpu::current_cpu.core_percent.insert(Cpu::current_cpu.core_percent.begin(), coreCount, {});
		Cpu::current_cpu.temp.insert(Cpu::current_cpu.temp.begin(), coreCount + 1, {});
		Cpu::core_old_active.insert(Cpu::core_old_active.begin(), coreCount, 0);
		Cpu::core_old_total.insert(Cpu::core_old_total.begin(), coreCount, 0);

		// Initialize Mem maps
		for (const auto& name : Mem::mem_names) Mem::current_mem.stats[name] = 0;
		for (const auto& name : Mem::mem_names) Mem::current_mem.percent[name] = {};
		for (const auto& name : Mem::swap_names) {
			Mem::current_mem.stats["swap_" + name] = 0;
			Mem::current_mem.percent["swap_" + name] = {};
		}
		Mem::current_mem.stats["swap_total"] = 0;
		Mem::current_mem.percent["swap_total"] = {};

		Cpu::cpuName = Cpu::get_cpuName();
		Cpu::collect();
		Mem::collect();
	}
}

namespace Tools {
	double system_uptime() {
		return system_time() / 1000000.0;
	}
}
