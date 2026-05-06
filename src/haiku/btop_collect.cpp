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
#include <unordered_set>
#include <pwd.h>
#include <stdlib.h>
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
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

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
	vector<string> available_fields = {"total", "user", "kernel", "idle"};
	vector<string> available_sensors = {"Auto"};
	std::unordered_map<int, int> core_mapping;
	tuple<int, float, long, string> current_bat;
	std::optional<std::string> container_engine;

	string get_cpuName() {
		system_info info;
		if (get_system_info(&info) == B_OK) {
			if (info.cpu_type >= B_CPU_INTEL_x86_586 && info.cpu_type < B_CPU_AMD_x86_64)
				return "Intel x86 CPU";
			if (info.cpu_type >= B_CPU_AMD_x86_64)
				return "AMD/Intel x64 CPU";
		}
		return "Haiku CPU";
	}

	auto collect(bool no_update) -> cpu_info& {
		if (Runner::stopping or (no_update and not current_cpu.cpu_percent.at("total").empty()))
			return current_cpu;

		system_info info;
		if (get_system_info(&info) != B_OK) return current_cpu;

		if (getloadavg(current_cpu.load_avg.data(), 3) < 0) {
			current_cpu.load_avg.fill(0.0);
		}

		long long now = system_time();
		long long global_active = 0;

		std::vector<::cpu_info> cpu_infos(Shared::coreCount);
		if (get_cpu_info(0, Shared::coreCount, cpu_infos.data()) != B_OK) {
			Logger::error("Cpu::collect() -> get_cpu_info() failed");
		} else {
			for (int i = 0; i < Shared::coreCount; i++) {
				long long active = cpu_infos[i].active_time;
				long long total = now;
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
				global_active += active;
			}
		}

		static long long old_global_active = 0;
		static long long old_global_total = 0;
		long long current_global_total = now * Shared::coreCount;

		long long diff_global_active = global_active - old_global_active;
		long long diff_global_total = current_global_total - old_global_total;
		old_global_active = global_active;
		old_global_total = current_global_total;

		if (diff_global_total > 0) {
			long long total_p = clamp((long long)round((double)diff_global_active * 100 / diff_global_total), 0ll, 100ll);
			current_cpu.cpu_percent.at("total").push_back(total_p);
			current_cpu.cpu_percent.at("idle").push_back(100 - total_p);
			current_cpu.cpu_percent.at("user").push_back(0);
			current_cpu.cpu_percent.at("kernel").push_back(0);
		} else {
			current_cpu.cpu_percent.at("total").push_back(0);
			current_cpu.cpu_percent.at("idle").push_back(100);
			current_cpu.cpu_percent.at("user").push_back(0);
			current_cpu.cpu_percent.at("kernel").push_back(0);
		}

		while (current_cpu.cpu_percent.at("total").size() > (size_t)width * 2) {
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
		static uint64_t totalMem = 0;
		if (totalMem > 0) return totalMem;
		system_info info;
		if (get_system_info(&info) == B_OK) {
			totalMem = (uint64_t)info.max_pages * B_PAGE_SIZE;
			return totalMem;
		}
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
			while (current_mem.percent[name].size() > (size_t)width * 2) current_mem.percent[name].pop_front();
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
				} else {
					Logger::debug("Mem::collect() -> fs_stat_dev() failed for dev {}", static_cast<int>(dev));
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
			Net::IfAddrsPtr if_addrs{};
			if (if_addrs.get_status() != 0) return empty_net;

			interfaces.clear();
			int sock = socket(AF_INET, SOCK_DGRAM, 0);
			for (auto* ifa = if_addrs.get(); ifa != nullptr; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr == nullptr) continue;
				const string iface = ifa->ifa_name;
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

				if (sock >= 0) {
					struct ifreq ifr;
					memset(&ifr, 0, sizeof(ifr));
					strncpy(ifr.ifr_name, iface.c_str(), IF_NAMESIZE);
					if (ioctl(sock, SIOCGIFSTATS, &ifr) == 0) {
						auto& stat = net[iface].stat;
						uint64_t rx = ifr.ifr_stats.receive_bytes;
						uint64_t tx = ifr.ifr_stats.send_bytes;

						for (const string& dir : {"download", "upload"}) {
							uint64_t val = (dir == "download") ? rx : tx;
							auto& s = stat[dir];
							if (val < s.last) s.rollover += s.last;
							if (timestamp > 0)
								s.speed = (val + s.rollover - s.last) * 1000 / max(1ULL, new_timestamp - timestamp);
							if (s.speed > s.top) s.top = s.speed;
							s.total = val + s.rollover - s.offset;
							s.last = val;

							auto& bw = net[iface].bandwidth[dir];
							bw.push_back(s.speed);
							if (bw.size() > (size_t)width * 2) bw.pop_front();
						}
					}
				}
			}
			if (sock >= 0) close(sock);
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

	static void _collect_details(const size_t pid, vector<proc_info>& procs) {
		if (pid != detailed.last_pid) {
			detailed = {};
			detailed.last_pid = pid;
		}

		auto p_info = std::find_if(procs.begin(), procs.end(), [pid](const auto& a) { return a.pid == pid; });
		if (p_info == procs.end()) return;
		detailed.entry = *p_info;

		double usage = detailed.entry.cpu_p;
		if (not Config::getB("proc_per_core")) usage *= Shared::coreCount;
		detailed.cpu_percent.push_back(clamp((long long)round(usage), 0ll, 100ll));
		while (detailed.cpu_percent.size() > (size_t)width) detailed.cpu_percent.pop_front();

		detailed.status = (proc_states.contains(detailed.entry.state)) ? proc_states.at(detailed.entry.state) : "Unknown";
		detailed.memory = floating_humanizer(detailed.entry.mem);

		detailed.mem_bytes.push_back(detailed.entry.mem);
		while (detailed.mem_bytes.size() > (size_t)width) detailed.mem_bytes.pop_front();
	}

	struct proc_times {
		bigtime_t user_time;
		bigtime_t kernel_time;
		bigtime_t timestamp;
	};
	std::unordered_map<size_t, proc_times> old_proc_times;

	auto collect(bool no_update) -> vector<proc_info>& {
		const auto show_detailed = Config::getB("show_detailed");
		const size_t detailed_pid_cfg = Config::getI("detailed_pid");
		const bool per_core = Config::getB("proc_per_core");

		if (Runner::stopping or (no_update and not current_procs.empty())) {
			if (show_detailed and detailed_pid_cfg != detailed.last_pid) _collect_details(detailed_pid_cfg, current_procs);
			return current_procs;
		}

		current_procs.clear();
		static std::unordered_set<size_t> found_pids;
		found_pids.clear();

		int32 team_cookie = 0;
		team_info ti;
		bigtime_t now = system_time();
		while (get_next_team_info(&team_cookie, &ti) == B_OK) {
			proc_info pi;
			pi.pid = ti.team;
			found_pids.insert(pi.pid);
			// Note: Parent team ID is not available in public Haiku team_info.
			pi.ppid = 0;

			string args = ti.args;
			size_t space = args.find(' ');
			string proc_path = (space == string::npos) ? args : args.substr(0, space);
			pi.name = fs::path(proc_path).filename().string();
			pi.cmd = args;
			pi.state = 'S';

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
				if (thi.state == B_THREAD_RUNNING) pi.state = 'R';
			}

			if (old_proc_times.contains(pi.pid)) {
				auto& old = old_proc_times.at(pi.pid);
				bigtime_t diff_time = (team_user_time + team_kernel_time) - (old.user_time + old.kernel_time);
				bigtime_t diff_period = now - old.timestamp;
				if (diff_period > 0) {
					const int cmult = (per_core) ? Shared::coreCount : 1;
					pi.cpu_p = clamp(round(cmult * 1000.0 * diff_time / (diff_period * Shared::coreCount)) / 10.0, 0.0, 100.0 * Shared::coreCount);
				}
			}
			old_proc_times[pi.pid] = {team_user_time, team_kernel_time, now};

			// Memory usage: resident set size by summing memory areas
			pi.mem = 0;
			ssize_t area_cookie = 0;
			area_info ai;
			status_t area_status;
			while ((area_status = get_next_area_info(ti.team, &area_cookie, &ai)) == B_OK) {
				pi.mem += ai.ram_size;
			}
			if (area_status != B_BAD_VALUE && area_status != B_BAD_TEAM_ID)
				Logger::debug("Proc::collect() -> get_next_area_info failed for team {}", static_cast<int>(ti.team));
			current_procs.push_back(pi);
		}

		if (show_detailed) {
			_collect_details(detailed_pid_cfg, current_procs);
		}

		std::erase_if(old_proc_times, [&](const auto& n) { return !found_pids.contains(n.first); });

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
		Cpu::current_cpu.cpu_percent["user"] = {};
		Cpu::current_cpu.cpu_percent["kernel"] = {};
		Cpu::current_cpu.cpu_percent["idle"] = {};

		Cpu::current_cpu.core_percent.insert(Cpu::current_cpu.core_percent.begin(), coreCount, {});
		Cpu::current_cpu.temp.insert(Cpu::current_cpu.temp.begin(), coreCount + 1, {});
		Cpu::core_old_active.insert(Cpu::core_old_active.begin(), coreCount, 0);
		Cpu::core_old_total.insert(Cpu::core_old_total.begin(), coreCount, 0);

		// Initialize Mem maps
		for (const auto& name : Mem::mem_names) {
			Mem::current_mem.stats[name] = 0;
			Mem::current_mem.percent[name] = {};
		}
		for (const auto& name : Mem::swap_names) {
			Mem::current_mem.stats[name] = 0;
			Mem::current_mem.percent[name] = {};
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
