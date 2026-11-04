# cdsp_state

Inspect the CDSP's **sleep state**, **clock**, and **DCVS clients** (which libraries are voting on
the CDSP) from the HLOS side. It can also run as a monitor: when the CDSP stays awake for too long,
it automatically records the clock and the DCVS clients at that moment.

Background: the "CDSP won't sleep" problem needs to be caught and diagnosed in normal user scenarios —
without pushing sysMonApp and without manually enabling debugfs to capture data.

## Layout

```
HLOS_state/
├── build.sh                         Build + push to the device (+ optionally run)
├── src/
│   ├── cdsp_state.cpp               User-space program
│   └── sysmon_subsystem_stats_ioctl.h   User-space ioctl definitions for the sysmon driver
└── kernel/                          Kernel-module option checkstate.ko (unused for now, see "How data is obtained")
    ├── checkstate.c, Kbuild
    ├── checkstate_ioctl.h           ioctl definitions shared by the kernel and user space
    ├── cdsprm_dcvs_clients_votes.h
    └── sysmon_subsystem_stats.c/.h  Reference copies of the original files
```

## Build and push: build.sh

Run this on a Linux machine that has access to the Android NDK and adb.

```
./build.sh                                   Build + push to the device
./build.sh run [args]                        Build + push, then run cdsp_state on the device
./build.sh --ndk PATH --serial SN run ...    Same, with the NDK and device specified explicitly
```

| Option / environment variable | Purpose |
|---|---|
| `--ndk PATH` or `NDK=PATH` | NDK root directory (the level that contains `toolchains/`). If given, it is used directly without searching; an invalid path is a hard error. |
| `--serial SN` or `SERIAL=SN` | adb device serial. If omitted: a single connected device is used automatically; zero or multiple devices is an error, and the devices are listed. |
| `NDK_SEARCH="dir..."` | Where to search for the NDK when it is not given. Defaults to `/`. |
| `run [args]` | Run on the device after pushing. Everything after `run` is passed to `cdsp_state` verbatim. |

Options must come before `run`.

### Search order when the NDK is not given

The search stops at the first level that succeeds:

1. `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`
2. `ANDROID_SDK_ROOT/ndk/*`, `ANDROID_HOME/ndk/*`
3. The directory of `ndk-build` on `PATH`
4. The cache of the last search, `~/.cache/cdsp_state_ndk`
5. A search downward from `/` for `android-ndk*` (up to 8 levels deep, at most 2 minutes, skipping
   `/proc`, `/sys`, `.git`, `.repo`, `out`, etc.)

Each candidate is checked for `aarch64-linux-android31-clang++` (candidates that are too old or
incomplete are skipped); when several match, the newest version wins. The result is written to the
cache, so subsequent runs on the same server are instant.

### Examples

```
./build.sh run                                           Find the NDK and device automatically; build, push, query once
./build.sh --ndk /path/to/android-ndk-r25c run sleep     Use a specific NDK; query sleep only
SERIAL=xxxx ./build.sh run --watch --logcat              Use a specific device; start monitoring
```

On the device the binary lives at `/data/local/tmp/cdsp_state`. Running it requires `adb root`
(the script does this for you).

## Run: cdsp_state

```
cdsp_state [--logcat] [--file PATH] [sleep] [clock] [dcvs]
cdsp_state [--logcat] [--file PATH] --watch [-i interval_s] [-t threshold_s]
```

### One-shot queries (for verification)

| Command | What it reports |
|---|---|
| `cdsp_state` | Sleep, clock, and DCVS in turn (all of them by default) |
| `cdsp_state sleep` | Sleep state: `IN LPM` / `ACTIVE`, LPM count, last entered/exited time, accumulated sleep time |
| `cdsp_state clock` | Core clock, votes (AB/IB, sleep latency), HMX/HVX, residency per clock/DDR level, Q6 load |
| `cdsp_state dcvs` | DCVS clients: the aggregated state, plus each client's Client ID, PID/TID, Lib Name, `Client Suspected to Prevent CDSP Sleep Issue`, etc. |

These can be combined, e.g. `cdsp_state sleep dcvs`.

Sleep rule: `last_entered_at > last_exited_at` means the CDSP is currently sleeping.

### Monitoring: --watch

| Argument | Default | Meaning |
|---|---|---|
| `-i N` | 5 | Read the sleep state every N seconds (an SMEM read; it does not wake the CDSP) |
| `-t N` | 30 | Raise an alert after N consecutive seconds without entering sleep |

How it works:

1. Each cycle compares the LPM `count` and accumulated time; if neither increased, the CDSP is
   considered not to have slept during that interval.
2. After `-t` seconds without sleep, it emits one `CDSP_NOT_SLEEPING` line and captures the clock
   and the DCVS clients **once**. It does not re-alert for the same wake episode.
3. When the CDSP starts sleeping again, it emits `CDSP_SLEEP_RECOVERED`.
4. While the CDSP is sleeping normally, it prints a single line at startup and then stays quiet.

A DCVS query wakes the CDSP, so it is issued only once after a non-sleeping episode is detected.
Time is measured with `CLOCK_BOOTTIME`, so time spent in HLOS suspend is counted too.

### Where output goes

| Destination | When | Location |
|---|---|---|
| Terminal | Always | The terminal running `adb shell` |
| logcat | `--logcat` | Device logcat, tag `cdsp_state`; alerts are at WARN level |
| File | `--file PATH` | PATH on the device, appended, one timestamped line per entry |

Recommended form for long-running monitoring:

```
./build.sh run --watch --logcat --file /data/local/tmp/cdsp_state.log
```

Viewing the records:

```
adb -s <SN> logcat -d -s cdsp_state
adb -s <SN> shell cat /data/local/tmp/cdsp_state.log
```

Running it in the background for the long term:

```
adb -s <SN> shell "nohup /data/local/tmp/cdsp_state --watch --logcat --file /data/local/tmp/cdsp_state.log > /dev/null 2>&1 &"
```

## How data is obtained

| Information | Preferred | Next | Last resort |
|---|---|---|---|
| sleep, clock | User-space ioctls on `/dev/sysmon_subsystem_stats` (sleep, Q6 votes, power residency) | `/dev/checkstate` (`kernel/checkstate.ko` calling the kernel APIs), when present | debugfs `sysmon_subsystem_stats/master_cdsp_stats` |
| dcvs | `/dev/checkstate`, when present | debugfs `compute/dcvs_clients`, then reading the new `cdsprm` lines from the kernel log | — |

On the device tested so far (canoe):

- the sysmon driver does **not** expose user-space ioctls for sleep / Q6 votes / power residency
  (they return `EINVAL`); only the utilization ioctls 1–4 are available;
- `checkstate.ko` is not loaded;
- so sleep, clock, and dcvs all fall back to **debugfs** in practice.

The debugfs path requires **root** and a build that allows mounting debugfs (a userdebug build). The
program mounts it automatically with `mount -t debugfs` and sets `vendor.fastrpc.process.attrs` to
`0x2000` so that DCVS clients report their library names.
