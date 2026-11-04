// cdsp_state: query / monitor CDSP sleep state, clock and DCVS clients (HLOS processes)
//
// sleep/clock: first the sysmon user-space ioctls (calculator_verify_FR FR5), which this device's sysmon
// driver does not have (EINVAL); then /dev/checkstate (kernel/checkstate.c), which calls the kernel APIs;
// without checkstate.ko, debugfs sysmon_subsystem_stats/master_cdsp_stats (root).
// dcvs: /dev/checkstate when loaded, else debugfs (root) and the cdsprm lines of the kernel log.
//   sleep : CHECKSTATE_IOC_SLEEP           -> sysmon_stats_query_sleep()
//   clock : CHECKSTATE_IOC_Q6_VOTES        -> sysmon_stats_query_q6_votes()
//           CHECKSTATE_IOC_POWER_RESIDENCY -> sysmon_stats_query_power_residency()
//           CHECKSTATE_IOC_DDR_RESIDENCY   -> sysmon_stats_query_ddr_residency()
//   dcvs  : CHECKSTATE_IOC_DCVS_CLIENTS    -> cdsprm_sysmon_dcvs_clients_votes()
//
// sleep/clock read SMEM and do not wake CDSP. dcvs is an rpmsg round trip that wakes CDSP,
// so --watch only queries it once CDSP is found not sleeping.
//
// Usage:
//   cdsp_state [sleep] [clock] [dcvs]           one-shot query (default: all), for verification
//   cdsp_state --watch [-i 5] [-t 30]           long-term monitor: sample LPM every -i s, alert with
//                                               clock and DCVS clients when CDSP has no LPM entry for -t s
//   --logcat      also write the output to logcat (tag cdsp_state; alerts at WARN)
//   --file PATH   also append the output, timestamped, to PATH
// Output always goes to stdout too. --watch only writes on start and on alerts.
//
// Build with the NDK (build.sh):
//   aarch64-linux-android31-clang++ -std=c++17 -O2 -Wall -static-libstdc++ -Isrc -Ikernel src/cdsp_state.cpp -llog -o cdsp_state

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>
#include <string>
#include <vector>

#include <android/log.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "cdsprm_dcvs_clients_votes.h"

// From sysmon_subsystem_stats.h, which is kernel-only. Must match the device's sysmon driver.
struct sleep_stats {
	uint32_t stat_type;
	uint32_t count;              // number of LPM entries
	uint64_t last_entered_at;    // arch timer ticks
	uint64_t last_exited_at;     // arch timer ticks
	uint64_t accumulated;        // ticks in LPM since boot, incl. an in-progress sleep
};

struct sysmon_smem_q6_event_stats {
	uint32_t QDSP6_clk;          // Q6 core clock (KHz)
	uint32_t Ab_vote_lsb;
	uint32_t Ab_vote_msb;
	uint32_t Ib_vote_lsb;
	uint32_t Ib_vote_msb;
	uint32_t Sleep_latency;      // sleep latency vote (us)
	int32_t HMX_Power_state;     // 1: ON, 0: OFF, -1: not supported
	int32_t HMX_clk;             // HMX clock (KHz), -1: not supported
};

struct sysmon_smem_power_stats {
	uint32_t version;            // >= 2: current_clk is valid
	uint32_t clk_arr[32];        // core clock frequency (KHz)
	uint32_t active_time[32];    // active time (s) per core clock
	uint32_t pc_time;            // power collapse time (s)
	uint32_t lpi_time;           // LPI time (s)
	uint32_t island_time[32];
	uint32_t current_clk;        // current core clock (KHz)
};

struct sysmon_smem_ddr_stats {
	uint32_t clk_arr[32];        // DDR clock frequency (KHz)
	uint32_t active_time[32];    // active time (s) per DDR clock
	uint32_t num_ddrclk_levels;
	uint32_t pc_time;            // DSP power collapse time (s)
	uint32_t curr_clk;           // current DDR clock (KHz)
};

#include "checkstate_ioctl.h"

// Shipped user-space ioctls of the sysmon driver, as used by calculator_verify_FR (FR5).
// The header uses the kernel typedef u32 in its ioctl numbers.
typedef uint32_t u32;
#include "sysmon_subsystem_stats_ioctl.h"

static_assert(sizeof(sysmon_smem_q6_event_stats) == sizeof(sysmon_smem_q6_event_stats_ioctl), "layout");
static_assert(sizeof(sysmon_smem_power_stats) == sizeof(sysmon_smem_power_stats_ioctl), "layout");

static const char kCheckstateDev[] = "/dev/checkstate";
static const char kDebugfsDir[] = "/sys/kernel/debug";
static const char kDcvsClientsNode[] = "/sys/kernel/debug/compute/dcvs_clients";
static const char kMasterCdspStats[] = "/sys/kernel/debug/sysmon_subsystem_stats/master_cdsp_stats";
static const uint64_t kTicksPerMs = 19200;  // arch timer, sysmon_subsystem_stats.c

static bool g_logcat;
static FILE *g_file;

static void vemit(int prio, const char *fmt, va_list ap)
{
	char line[512];

	vsnprintf(line, sizeof(line), fmt, ap);
	fputs(line, stdout);
	if (g_logcat)
		__android_log_write(prio, "cdsp_state", line);
	if (g_file) {
		char ts[32];
		time_t t = time(nullptr);
		struct tm tm;

		strftime(ts, sizeof(ts), "%F %T", localtime_r(&t, &tm));
		fprintf(g_file, "%s %s", ts, line);
	}
}

// Print to stdout, and to logcat / --file when enabled
__attribute__((format(printf, 1, 2)))
static void out(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vemit(ANDROID_LOG_INFO, fmt, ap);
	va_end(ap);
}

// Same as out(), at WARN in logcat
__attribute__((format(printf, 1, 2)))
static void alert(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vemit(ANDROID_LOG_WARN, fmt, ap);
	va_end(ap);
}

// ioctl on /dev/checkstate; prints the error and returns false on failure
static bool checkstate_ioctl(unsigned long cmd, void *buf, const char *what)
{
	int fd = open(kCheckstateDev, O_RDONLY | O_CLOEXEC);
	int ret = fd < 0 ? -1 : ioctl(fd, cmd, buf);
	int err = errno;

	if (fd >= 0)
		close(fd);
	if (ret < 0) {
		out("%s: %s failed: %s%s\n", what, kCheckstateDev, strerror(err),
		    err == ENOENT ? " (is checkstate.ko loaded?)" :
		    err == ENOTTY ? " (checkstate.ko built from another checkstate_ioctl.h / struct layout)" :
		    err == ENOKEY ? " (not published by CDSP on this target)" : "");
		return false;
	}
	return true;
}

// ioctl on /dev/sysmon_subsystem_stats, no kernel module needed; false with errno in *err on failure
static bool sysmon_ioctl(unsigned long cmd, void *buf, int *err)
{
	int fd = open(SUBSYSTEMSTATS_DEVICE_NAME, O_RDONLY | O_CLOEXEC);
	int ret = fd < 0 ? -1 : ioctl(fd, cmd, buf);

	*err = errno;
	if (fd >= 0)
		close(fd);
	return ret >= 0;
}

static void sysmon_failed(const char *what, int err)
{
	out("%s: %s failed: %s\n", what, SUBSYSTEMSTATS_DEVICE_NAME, strerror(err));
}

// Mount debugfs if needed; prints the error and returns false on failure
static bool mount_debugfs()
{
	if (mount("none", kDebugfsDir, "debugfs", 0, nullptr) < 0 && errno != EBUSY) {
		out("mount debugfs failed: %s (needs root and debugfs)\n", strerror(errno));
		return false;
	}
	return true;
}

// The sysmon driver's own dump of the sleep, clock and DDR stats
static bool read_master_cdsp_stats(std::string *text)
{
	char buf[512];
	size_t n;

	if (!mount_debugfs())
		return false;
	FILE *f = fopen(kMasterCdspStats, "re");
	if (!f) {
		out("open %s failed: %s\n", kMasterCdspStats, strerror(errno));
		return false;
	}
	text->clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text->append(buf, n);
	fclose(f);
	return true;
}

// Number after the "=" of a "<key> = <number>" line of master_cdsp_stats
static bool stats_value(const std::string &text, const char *key, uint64_t *v)
{
	size_t p = text.find(key);

	if (p == std::string::npos)
		return false;
	p = text.find('=', p);
	if (p == std::string::npos)
		return false;
	*v = strtoull(text.c_str() + p + 1, nullptr, 10);
	return true;
}

// ---------------------------------------------------------------------------
// sleep

struct SleepState {
	bool in_lpm;
	uint32_t count;
	uint64_t last_entered_at;  // arch timer ticks
	uint64_t last_exited_at;   // arch timer ticks
	uint64_t accumulated_ms;
};

static bool query_sleep(SleepState *s)
{
	sleep_stats l = {};
	sleep_stats_ioctl_ext e = {};
	int err;

	e.struct_size = sizeof(e);
	e.sleep_stats_driver_capability_mask = SLEEP_STATS_DEFAULT_FEATURE_MASK;
	if (sysmon_ioctl(Q6_CDSP_SLEEP_STATS, &e, &err)) {
		memcpy(&l, &e.sleep_stats, sizeof(l));
	} else if (access(kCheckstateDev, F_OK) == 0) {
		if (!checkstate_ioctl(CHECKSTATE_IOC_SLEEP, &l, "CDSP sleep")) {
			sysmon_failed("CDSP sleep", err);
			return false;
		}
	} else {
		// no ioctl and no checkstate.ko: the LPM stats of master_cdsp_stats
		std::string t;
		uint64_t count = 0;

		if (!read_master_cdsp_stats(&t)) {
			sysmon_failed("CDSP sleep", err);
			return false;
		}
		if (!stats_value(t, "Count =", &count) || !stats_value(t, "Last Entered At =", &l.last_entered_at) ||
		    !stats_value(t, "Last Exited At =", &l.last_exited_at) ||
		    !stats_value(t, "Accumulated Duration =", &l.accumulated)) {
			out("CDSP sleep: no LPM stats in %s\n", kMasterCdspStats);
			return false;
		}
		l.count = count;
	}
	s->in_lpm = l.last_entered_at > l.last_exited_at;
	s->count = l.count;
	s->last_entered_at = l.last_entered_at;
	s->last_exited_at = l.last_exited_at;
	// accumulated already includes an in-progress sleep
	s->accumulated_ms = l.accumulated / kTicksPerMs;
	return true;
}

static bool report_sleep()
{
	SleepState s;

	if (!query_sleep(&s))
		return false;
	out("CDSP sleep: %s, LPM count %u, last entered %llu, last exited %llu, accumulated %llu ms\n",
	    s.in_lpm ? "IN LPM" : "ACTIVE", s.count,
	    (unsigned long long)s.last_entered_at, (unsigned long long)s.last_exited_at,
	    (unsigned long long)s.accumulated_ms);
	return true;
}

// ---------------------------------------------------------------------------
// clock

// master_cdsp_stats already lists the clock, votes, residency and DDR stats: print it, blank lines dropped
static bool report_clock_debugfs()
{
	std::string t;
	size_t pos = 0;

	if (!read_master_cdsp_stats(&t))
		return false;
	while (pos < t.size()) {
		size_t end = t.find('\n', pos);
		if (end == std::string::npos)
			end = t.size();
		if (end > pos)
			out("%.*s\n", (int)(end - pos), t.c_str() + pos);
		pos = end + 1;
	}
	return true;
}

static bool report_clock()
{
	sysmon_smem_q6_event_stats v = {};
	sysmon_smem_power_stats r = {};
	sysmon_smem_ddr_stats d = {};
	sysmon_smem_q6_event_stats_ioctl_ext ve = {};
	sysmon_smem_power_stats_ioctl_ext re = {};
	bool ok = true, got;
	int err;

	ve.struct_size = sizeof(ve);
	ve.q6_event_stats_driver_capability_mask = Q6_EVENT_STATS_DEFAULT_FEATURE_MASK;
	got = sysmon_ioctl(Q6_CDSP_VOTES, &ve, &err);
	if (!got && access(kCheckstateDev, F_OK) < 0)
		return report_clock_debugfs();
	if (got)
		memcpy(&v, &ve.q6_event_stats, sizeof(v));
	else
		got = checkstate_ioctl(CHECKSTATE_IOC_Q6_VOTES, &v, "CDSP clock");
	if (got) {
		out("CDSP clock: core %u KHz, AB %llu B, IB %llu B, sleep latency vote %u us (0 = none)\n",
		    v.QDSP6_clk,
		    ((unsigned long long)v.Ab_vote_msb << 32) | v.Ab_vote_lsb,
		    ((unsigned long long)v.Ib_vote_msb << 32) | v.Ib_vote_lsb,
		    v.Sleep_latency);
		if (v.HMX_Power_state >= 0)
			out("CDSP HMX: %s, %d KHz\n", v.HMX_Power_state ? "ON" : "OFF", v.HMX_clk);
	} else {
		sysmon_failed("CDSP clock", err);
		ok = false;
	}

	re.struct_size = sizeof(re);
	re.power_stats_driver_capability_mask = POWER_STATS_DEFAULT_FEATURE_MASK;
	got = sysmon_ioctl(Q6_CDSP_POWER_RESIDENCY, &re, &err);
	if (got)
		memcpy(&r, &re.power_stats, sizeof(r));
	else
		got = checkstate_ioctl(CHECKSTATE_IOC_POWER_RESIDENCY, &r, "CDSP clock residency");
	if (got) {
		for (size_t i = 0; i < std::size(r.clk_arr); i++)
			if (r.clk_arr[i])
				out("CDSP core clock level %2zu: %u KHz, active %u s\n",
				    i, r.clk_arr[i], r.active_time[i]);
		out("CDSP power collapse %u s, LPI %u s\n", r.pc_time, r.lpi_time);
		if (r.version >= 2)
			out("CDSP current core clock %u KHz\n", r.current_clk);
	} else {
		sysmon_failed("CDSP clock residency", err);
		ok = false;
	}

	// the sysmon driver has no DDR ioctl, only checkstate.ko provides it
	if (access(kCheckstateDev, F_OK) == 0) {
		if (checkstate_ioctl(CHECKSTATE_IOC_DDR_RESIDENCY, &d, "CDSP DDR residency")) {
			unsigned n = std::min<unsigned>(d.num_ddrclk_levels, std::size(d.clk_arr));
			for (unsigned i = 0; i < n; i++)
				out("CDSP DDR clock level %2u: %u KHz, active %u s\n", i, d.clk_arr[i], d.active_time[i]);
			out("CDSP DDR: %u levels, power collapse %u s, current %u KHz\n",
			    d.num_ddrclk_levels, d.pc_time, d.curr_clk);
		} else {
			ok = false;
		}
	}
	return ok;
}

// ---------------------------------------------------------------------------
// DCVS clients

static std::string read_proc(const std::string &path)
{
	char buf[256];
	FILE *f = fopen(path.c_str(), "re");

	if (!f)
		return "";
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	std::replace(buf, buf + n, '\0', ' ');  // cmdline args are NUL separated
	std::string s(buf, n);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\n'))
		s.pop_back();
	return s;
}

// HLOS process/thread behind a DCVS client, from its hlos_pid/hlos_tid
static std::string hlos_process(unsigned pid, unsigned tid)
{
	if (!pid)
		return "n/a (hlos_pid not reported on this target)";
	std::string proc = "/proc/" + std::to_string(pid);
	std::string name = read_proc(proc + "/cmdline");
	if (name.empty())
		name = read_proc(proc + "/comm");
	if (name.empty())
		return "exited";
	if (tid) {
		std::string thread = read_proc(proc + "/task/" + std::to_string(tid) + "/comm");
		if (!thread.empty())
			name += ", thread " + thread;
	}
	return name;
}

// Whole kernel log, not cleared (like dmesg)
static std::string read_klog()
{
	int size = klogctl(10 /* SYSLOG_ACTION_SIZE_BUFFER */, nullptr, 0);

	if (size <= 0)
		return "";
	std::string buf(size, '\0');
	int n = klogctl(3 /* SYSLOG_ACTION_READ_ALL */, &buf[0], size);

	buf.resize(n > 0 ? n : 0);
	return buf;
}

// cdsprm lines of a kernel log, one per element
static std::vector<std::string> cdsprm_lines(const std::string &klog)
{
	std::vector<std::string> v;
	size_t pos = 0;

	while (pos < klog.size()) {
		size_t end = klog.find('\n', pos);
		if (end == std::string::npos)
			end = klog.size();
		std::string line = klog.substr(pos, end - pos);
		if (line.find("cdsprm") != std::string::npos)
			v.push_back(line);
		pos = end + 1;
	}
	return v;
}

// Same as: setprop vendor.fastrpc.process.attrs 0x2000; mount -t debugfs none /sys/kernel/debug;
//          cat /sys/kernel/debug/compute/dcvs_clients; dmesg | grep cdsprm
// Needs root and a build that allows debugfs. Prints the cdsprm lines the read added to the kernel log.
static bool report_dcvs_debugfs()
{
	__system_property_set("vendor.fastrpc.process.attrs", "0x2000");  // show lib names
	if (!mount_debugfs())
		return false;

	std::vector<std::string> before = cdsprm_lines(read_klog());
	int fd = open(kDcvsClientsNode, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		out("CDSP DCVS: open %s failed: %s\n", kDcvsClientsNode, strerror(errno));
		return false;
	}
	char tmp[64];
	while (read(fd, tmp, sizeof(tmp)) > 0)
		;  // the read triggers the query, the result goes to the kernel log
	close(fd);

	// lines after the last one seen before the read (the kernel log is not cleared)
	std::vector<std::string> after = cdsprm_lines(read_klog());
	size_t start = 0;
	if (!before.empty()) {
		auto it = std::find(after.rbegin(), after.rend(), before.back());
		if (it != after.rend())
			start = after.rend() - it;
	}
	if (start >= after.size()) {
		out("CDSP DCVS: no cdsprm output in the kernel log\n");
		return false;
	}
	for (size_t i = start; i < after.size(); i++)
		out("%s\n", after[i].c_str());
	return true;
}

static bool report_dcvs()
{
	// no checkstate.ko: fall back to debugfs
	if (access(kCheckstateDev, F_OK) < 0 && errno == ENOENT)
		return report_dcvs_debugfs();

	sysmon_dcvs_clients info;

	if (!checkstate_ioctl(CHECKSTATE_IOC_DCVS_CLIENTS, &info, "CDSP DCVS"))
		return false;
	if (info.status != DCVS_CLIENTS_VOTES_FETCH_SUCCESS) {
		out("CDSP DCVS: invalid data, status %u\n", info.status);
		return false;
	}

	const auto &a = info.aggInfo;
	out("CDSP DCVS: %u clients, core corner %u/%u/%u (%u MHz), bus corner %u/%u/%u (%u MHz)\n",
	    info.num_clients,
	    a.core_target_corner, a.core_min_corner, a.core_max_corner, a.core_clk_vote_mhz,
	    a.bus_target_corner, a.bus_min_corner, a.bus_max_corner, a.bus_clk_vote_mhz);
	out("CDSP DCVS: sleep latency %u us, override %u us, final %u us, lpm_state %u, HMX %s %u MHz\n",
	    a.sleep_latency, a.sleep_latency_override, a.final_sleep_latency_vote, a.lpm_state,
	    a.hmx_power ? "ON" : "OFF", a.hmx_clock_mhz);

	unsigned num = std::min<unsigned>(info.num_clients, DCVS_CLIENTS_TABLE_SIZE);
	for (unsigned i = 0; i < num; i++) {
		const auto &c = info.table[i];

		out("  [%u] %.*s client_id %u, HLOS pid %u tid %u: %s\n",
		    i, (int)sizeof(c.libname), (const char *)c.libname, c.client_id,
		    c.hlos_pid, c.hlos_tid, hlos_process(c.hlos_pid, c.hlos_tid).c_str());
		out("      active %d, dcvs_enable %d, latency %u us%s, mips_latency %u us%s, sleep_disable %u%s\n",
		    c.active_client, c.dcvs_enable,
		    c.latency, c.set_latency ? "" : " (unset)",
		    c.mips_latency, c.mips_set_latency ? "" : " (unset)",
		    c.sleep_disable, c.set_sleep_disable ? "" : " (unset)");
		if (c.set_core_params)
			out("      core corner %u/%u/%u\n",
			    c.core_target_corner, c.core_min_corner, c.core_max_corner);
		if (c.mips_set_mips)
			out("      mips per thread %u, total %u\n", c.mips_mipsPerThread, c.mips_mipsTotal);
	}
	return true;
}

// ---------------------------------------------------------------------------
// monitor

static int64_t boottime_ms()
{
	timespec ts;

	// boottime: a CDSP that stays awake across HLOS suspend is caught on resume
	clock_gettime(CLOCK_BOOTTIME, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static int watch(unsigned interval_s, unsigned threshold_s)
{
	SleepState prev = {};
	bool have_prev = false, no_lpm = false, alerted = false;
	int64_t prev_t = 0, no_lpm_since = 0;

	out("watching CDSP LPM every %u s, alert after %u s without LPM entry\n", interval_s, threshold_s);
	for (;;) {
		SleepState s;
		int64_t now = boottime_ms();

		if (query_sleep(&s)) {
			if (!have_prev) {
				have_prev = true;
			} else if (s.count != prev.count || s.accumulated_ms > prev.accumulated_ms) {
				if (alerted)
					alert("CDSP_SLEEP_RECOVERED after %lld ms without LPM, LPM count %u\n",
					      (long long)(now - no_lpm_since), s.count);
				no_lpm = alerted = false;
			} else {
				if (!no_lpm) {
					no_lpm = true;
					no_lpm_since = prev_t;
				}
				if (!alerted && now - no_lpm_since >= threshold_s * 1000LL) {
					alerted = true;
					alert("CDSP_NOT_SLEEPING for %lld ms, LPM count %u, accumulated %llu ms\n",
					      (long long)(now - no_lpm_since), s.count,
					      (unsigned long long)s.accumulated_ms);
					report_clock();
					report_dcvs();
				}
			}
			prev = s;
			prev_t = now;
		} else if (!have_prev) {
			return 1;  // never got a sample, e.g. checkstate.ko not loaded
		}
		fflush(stdout);
		sleep(interval_s);
	}
}

// ---------------------------------------------------------------------------

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [--logcat] [--file PATH] [sleep] [clock] [dcvs]\n"
		"       %s [--logcat] [--file PATH] --watch [-i interval_s] [-t threshold_s]\n", prog, prog);
}

int main(int argc, char **argv)
{
	bool do_sleep = false, do_clock = false, do_dcvs = false, do_watch = false;
	int interval_s = 5, threshold_s = 30;

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];

		if (a == "sleep") {
			do_sleep = true;
		} else if (a == "clock") {
			do_clock = true;
		} else if (a == "dcvs") {
			do_dcvs = true;
		} else if (a == "--watch") {
			do_watch = true;
		} else if (a == "-i" && i + 1 < argc) {
			interval_s = std::max(1, atoi(argv[++i]));
		} else if (a == "-t" && i + 1 < argc) {
			threshold_s = std::max(0, atoi(argv[++i]));
		} else if (a == "--logcat") {
			g_logcat = true;
		} else if (a == "--file" && i + 1 < argc) {
			g_file = fopen(argv[++i], "ae");
			if (!g_file) {
				fprintf(stderr, "open %s failed: %s\n", argv[i], strerror(errno));
				return 1;
			}
			setvbuf(g_file, nullptr, _IOLBF, 0);  // a killed monitor keeps what it wrote
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (do_watch)
		return watch(interval_s, threshold_s);

	if (!do_sleep && !do_clock && !do_dcvs)
		do_sleep = do_clock = do_dcvs = true;
	bool ok = true;
	if (do_sleep)
		ok &= report_sleep();
	if (do_clock)
		ok &= report_clock();
	if (do_dcvs)
		ok &= report_dcvs();
	return ok ? 0 : 1;
}
