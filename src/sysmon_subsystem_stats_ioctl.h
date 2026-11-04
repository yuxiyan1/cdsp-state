/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_SYSMON_SUBSYSTEM_STATS_IOCTL_H__
#define __QCOM_SYSMON_SUBSYSTEM_STATS_IOCTL_H__

#include <linux/types.h>

/* Maximum number of clock levels in power stats */
#define SYSMON_POWER_STATS_MAX_CLK_LEVELS   32

/**
 * struct smem_bw_stats_ioctl - IOCTL payload for bandwidth statistics
 *
 * This structure is used by user space to retrieve bandwidth monitoring
 * statistics from the kernel via IOCTL.
 *
 * Bandwidth statistics capability mask:
 *
 * Each bit in @bw_stats_driver_capability_mask indicates whether the
 * corresponding field in this structure is valid and supported by the
 * kernel driver.
 *
 * Bit 0 (BIT(0)) represents the default capability set, which covers
 * the following fields:
 *   - bw_sample_in_kbps
 *   - sampling_window_in_ms
 *   - accum_bw_mon_val
 *   - last_updated_timestamp
 *   - current_timestamp
 *
 * Additional bits may be defined in the future to indicate optional or
 * extended bandwidth monitoring capabilities.
 *
 * @struct_size:
 *      Size of this structure in bytes. Used for ABI compatibility checks
 *      between user space and kernel.
 *
 * @bw_stats_driver_capability_mask:
 *      Bitmask indicating the bandwidth statistics capabilities supported
 *      by the kernel driver.
 *
 * @bw_sample_in_kbps:
 *      Latest computed average bandwidth sample in kilobits per second (kbps).
 *
 * @sampling_window_in_ms:
 *      Sampling window duration (in milliseconds) used to compute the
 *      average BW_MON value.
 *
 * @accum_bw_mon_val:
 *      Total accumulated bandwidth value in bytes since accumulation began.
 *
 * @last_updated_timestamp:
 *      Timestamp indicating when the bandwidth statistics were last updated
 *      in SMEM.
 *
 * @current_timestamp:
 *      Timestamp captured when the bandwidth statistics were read from SMEM.
 */
struct smem_bw_stats_ioctl {
	__u32 struct_size;

	__u32 bw_stats_driver_capability_mask;

	__u32 bw_sample_in_kbps;

	__u32 sampling_window_in_ms;

	__u64 accum_bw_mon_val;

	__u64 last_updated_timestamp;

	__u64 current_timestamp;
};

/**
 * struct sleep_stats_ioctl - CX collapse sleep statistics
 *
 * @stat_type:
 *      Sleep statistics type or version identifier.
 *
 * @count:
 *      Number of times the subsystem voted for AOSS Sleep with CX Collapse.
 *
 * @last_entered_at:
 *      Timestamp of the most recent CX collapse entry.
 *
 * @last_exited_at:
 *      Timestamp of the most recent CX collapse exit.
 *
 * @accumulated:
 *      Total accumulated duration spent in CX collapse sleep state.
 */
struct sleep_stats_ioctl {
	__u32 stat_type;
	__u32 count;
	__u64 last_entered_at;
	__u64 last_exited_at;
	__u64 accumulated;
};

/**
 * struct sleep_stats_ioctl_ext - Extended CX sleep statistics IOCTL payload
 *
 * Sleep statistics capability mask:
 *
 * Each bit in @sleep_stats_driver_capability_mask indicates whether the
 * corresponding sleep statistics fields are supported by the kernel driver.
 *
 * Bit 0 (BIT(0)) represents the default capability set, which covers
 * the following fields:
 *   - stat_type
 *   - count
 *   - last_entered_at
 *   - last_exited_at
 *   - accumulated
 *
 * Additional bits may be defined in the future to support extended
 * sleep or power state statistics.
 *
 * @struct_size:
 *      Size of this structure in bytes for ABI compatibility validation.
 *
 * @sleep_stats_driver_capability_mask:
 *      Bitmask indicating supported sleep statistics capabilities.
 *
 * @sleep_stats:
 *      CX collapse sleep statistics payload.
 */
struct sleep_stats_ioctl_ext {
	__u32 struct_size;
	__u32 sleep_stats_driver_capability_mask;
	struct sleep_stats_ioctl sleep_stats;
};

/**
 * struct sysmon_smem_q6_event_stats_ioctl - DSP Q6 event-based statistics
 *
 * This structure contains SMEM-reported event statistics for the QDSP6
 * core, including clocking, bandwidth votes, sleep latency, and HMX state.
 *
 * @QDSP6_clk:
 *      QDSP6 core clock frequency in kilohertz (KHz).
 *
 * @Ab_vote_lsb:
 *      Lower 32 bits of the average bandwidth vote (bytes).
 *
 * @Ab_vote_msb:
 *      Upper 32 bits of the average bandwidth vote (bytes).
 *
 * @Ib_vote_lsb:
 *      Lower 32 bits of the instantaneous bandwidth vote (bytes).
 *
 * @Ib_vote_msb:
 *      Upper 32 bits of the instantaneous bandwidth vote (bytes).
 *
 * @Sleep_latency:
 *      Sleep latency vote in microseconds.
 *
 * @HMX_Power_state:
 *      HMX power state:
 *        1  - enabled
 *        0  - disabled
 *       -1  - not supported
 *
 * @HMX_clk:
 *      HMX clock frequency in kilohertz (KHz), or -1 if unsupported.
 */
struct sysmon_smem_q6_event_stats_ioctl {
	__u32 QDSP6_clk;
	__u32 Ab_vote_lsb;
	__u32 Ab_vote_msb;
	__u32 Ib_vote_lsb;
	__u32 Ib_vote_msb;
	__u32 Sleep_latency;
	int HMX_Power_state;
	int HMX_clk;
};

/**
 * struct sysmon_smem_q6_event_stats_ioctl_ext - Extended DSP Q6 event statistics
 *
 * Q6 event statistics capability mask:
 *
 * Each bit in @q6_event_stats_driver_capability_mask indicates whether the
 * corresponding Q6 event statistics fields are supported by the driver.
 *
 * Bit 0 (BIT(0)) represents the default capability set, which covers
 * the following fields:
 *   - QDSP6_clk
 *   - Ab_vote_lsb
 *   - Ab_vote_msb
 *   - Ib_vote_lsb
 *   - Ib_vote_msb
 *   - Sleep_latency
 *   - HMX_Power_state
 *   - HMX_clk
 *
 * Additional bits may be introduced to support extended DSP event metrics.
 *
 * @struct_size:
 *      Size of this structure in bytes for ABI compatibility validation.
 *
 * @q6_event_stats_driver_capability_mask:
 *      Bitmask indicating supported Q6 event statistics capabilities.
 *
 * @q6_event_stats:
 *      DSP Q6 event statistics payload.
 */
struct sysmon_smem_q6_event_stats_ioctl_ext {
	__u32 struct_size;
	__u32 q6_event_stats_driver_capability_mask;
	struct sysmon_smem_q6_event_stats_ioctl q6_event_stats;
};

/**
 * struct sysmon_smem_power_stats_ioctl - DSP power and clock statistics
 *
 * This structure provides power usage statistics for the DSP, including
 * clock residency and time spent in low-power modes.
 *
 * @version:
 *      Version of the power statistics format.
 *
 * @clk_arr:
 *      Array of supported DSP clock frequencies in KHz.
 *
 * @active_time:
 *      Active time (seconds) spent at each clock frequency.
 *
 * @pc_time:
 *      Total time (seconds) spent in Low Power Mode (LPM).
 *
 * @lpi_time:
 *      Total time (seconds) spent in Low Power Island (LPI).
 *
 * @island_time:
 *      Time (seconds) spent in LPI per clock level.
 *
 * @current_clk:
 *      Current DSP core clock frequency in KHz.
 *      This field is valid only when version >= 2.
 */
struct sysmon_smem_power_stats_ioctl {
	__u32 version;
	__u32 clk_arr[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	__u32 active_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	__u32 pc_time;
	__u32 lpi_time;
	__u32 island_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	__u32 current_clk;
};

/**
 * struct sysmon_smem_power_stats_ioctl_ext - Extended DSP power statistics
 *
 * Power statistics capability mask:
 *
 * Each bit in @power_stats_driver_capability_mask indicates whether the
 * corresponding DSP power statistics fields are supported by the driver.
 *
 * Bit 0 (BIT(0)) represents the default capability set, which covers
 * the following fields:
 *   - version
 *   - clk_arr
 *   - active_time
 *   - pc_time
 *   - lpi_time
 *   - island_time
 *   - current_clk
 *
 * Additional capability bits may be defined for future power metrics.
 *
 * @struct_size:
 *      Size of this structure in bytes for ABI compatibility validation.
 *
 * @power_stats_driver_capability_mask:
 *      Bitmask indicating supported power statistics capabilities.
 *
 * @power_stats:
 *      DSP power and clock statistics payload.
 */
struct sysmon_smem_power_stats_ioctl_ext {
	__u32 struct_size;
	__u32 power_stats_driver_capability_mask;
	struct sysmon_smem_power_stats_ioctl power_stats;
};

/* BitMask Flag for BW stats default capability set */
#define BWMON_STATS_DEFAULT_FEATURE_MASK	0x1

/* BitMask Flag for sleep stats default capability set */
#define SLEEP_STATS_DEFAULT_FEATURE_MASK	0x1

/* BitMask Flag for event stats default capability set */
#define Q6_EVENT_STATS_DEFAULT_FEATURE_MASK	0x1

/* BitMask Flag for Power stats default capability set */
#define POWER_STATS_DEFAULT_FEATURE_MASK	0x1

/* Device name for SUBSYSTEM STATS */
#define SUBSYSTEMSTATS_DEVICE_NAME             "/dev/sysmon_subsystem_stats"

/* IOCTL to get HMX utilization */
#define HVX_UTILIZATION_QUERY _IOR('q', 1, u32)
/* IOCTL to get HVX utilization */
#define HMX_UTILIZATION_QUERY _IOR('q', 2, u32)
/* IOCTL to get q6 utilization */
#define Q6_CDSP_UTILIZATION _IOR('q', 3, u32)
/* IOCTL to get HLX utilization */
#define HLX_UTILIZATION_QUERY _IOR('q', 4, u32)
/* IOCTL to get BandWidth stats */
#define Q6_CDSP_BWMON_STATS _IOWR('q', 5, struct smem_bw_stats_ioctl)
/* IOCTL to get Sleep stats */
#define Q6_CDSP_SLEEP_STATS _IOWR('q', 6, struct sleep_stats_ioctl_ext)
/* IOCTL to get Q6 votes */
#define Q6_CDSP_VOTES _IOWR('q', 7, struct sysmon_smem_q6_event_stats_ioctl_ext)
/* IOCTL to get CDSP Power Residency */
#define Q6_CDSP_POWER_RESIDENCY _IOWR('q', 8, struct sysmon_smem_power_stats_ioctl_ext)

#endif /* __QCOM_SUBSYSTEMSTATS_H__ */
