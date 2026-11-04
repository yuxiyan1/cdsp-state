// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "%s: " fmt, KBUILD_MODNAME

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/soc/qcom/sysmon_subsystem_stats.h>
#include <uapi/misc/sysmon_subsystem_stats_ioctl.h>

struct subsystem_stats {
	dev_t devno;
	struct device *dev;
	struct cdev sysmon_cdev;
	struct class *dev_class;
};
static struct subsystem_stats g_subsystem_stats;
#define SUBSYSTEMSTATS_CLASS_NAME_LOCAL                          "class_subsystem_stats"
#define SUBSYSTEMSTATS_DEVICE_NAME_LOCAL                         "sysmon_subsystem_stats"
#define SYSMON_SMEM_ID					634
#define SLEEPSTATS_SMEM_ID_ADSP			606
#define SLEEPSTATS_SMEM_ID_CDSP			607
#define SLEEPSTATS_SMEM_ID_SLPI			608
#define SLEEPSTATS_LPI_SMEM_ID			613
#define DSPPMSTATS_SMEM_ID				624
#define SYS_CLK_TICKS_PER_MS			19200
#define SYS_CLK_TICKS_PER_SEC			19200000
#define DSPPMSTATS_NUMPD				5
#define HMX_HVX_PMU_EVENTS_NA			0xFFFF
#define HLX_PMU_EVENTS_NA				0xFFFF

#define SYSMON_POWER_STATS_MAX_CLK_LEVELS_16 16

struct pd_clients {
	int pid;
	u32 num_active;
};

struct dsppm_stats {
	u32 version;
	u32 latency_us;
	u32 timestamp;
	struct pd_clients pd[DSPPMSTATS_NUMPD];
};

struct sysmon_smem_power_stats_local {
	u32 version;
	/**< Version */

	u32 clk_arr[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< Core clock frequency(KHz) array */

	u32 active_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< Active time(seconds) array corresponding to core clock array */

	u32 pc_time;
	/**< DSP LPM(Low Power Mode) time(seconds) */

	u32 lpi_time;
	/**< DSP LPI(Low Power Island Mode) time(seconds) */

	u32 island_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< DSP LPI(Low Power Island Mode) time(seconds)
	 * array corresponding to core clock array
	 */

	u32 current_clk;
	/**< DSP current clock in KHz*/
};

struct sysmon_smem_power_stats_ext {
	u64 active_time_us[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< Active time(micro seconds) array corresponding to core clock array */

	u64 pc_time_us;
	/**< DSP LPM(Low Power Mode) time(micro seconds) */

	u64 check_sum;
	/**< Checksum */

	u32 clk_arr[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< Additional Core clock frequency(KHz) array (if clock levels more than
	 * SYSMON_POWER_STATS_MAX_CLK_LEVELS_16)
	 */

	u32 active_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< Active time(seconds) array corresponding to additional core clock array */

	u32 island_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< DSP LPI(Low Power Island Mode) time(seconds)
	 * array corresponding to additional core clock array
	 */

	u64 active_time_us2[SYSMON_POWER_STATS_MAX_CLK_LEVELS_16];
	/**< Active time(micro seconds) array corresponding to
	 * additional core clock array
	 */

	u64 island_time_us[SYSMON_POWER_STATS_MAX_CLK_LEVELS];
	/**< DSP LPI(Low Power Island Mode) time(micor seconds)
	 * array corresponding to core clock array
	 */

	u64 lpi_time_us;
	/**< DSP LPI(Low Power Island Mode) time(micro seconds) */

	u64 check_sum2;
	/**< Check sum */
};

struct sysmon_smem_power_stats_extended {
	struct sysmon_smem_power_stats_local powerstats;
	/**< Powerstats data */

	u32 last_update_time_powerstats_lsb;
	/**< Powerstats updated timestamp (lower significant 32 bits) */

	u32 last_update_time_powerstats_msb;
	/**< Powerstats updated timestamp (Most significant 32 bits) */

	struct sysmon_smem_power_stats_ext powerstats_ext;
	/**< Extended powerstats params */
};

struct sysmon_smem_q6_event_stats_local {
	u32 QDSP6_clk;
	/**< Q6 core clock in KHz */

	u32 Ab_vote_lsb;
	/**< Lower 32bits of the average bandwidth vote in bytes */

	u32 Ab_vote_msb;
	/**< Upper 32bits of the average bandwidth vote in bytes */

	u32 Ib_vote_lsb;
	/**< Lower 32bits of instantaneous bandwidth vote in bytes */

	u32 Ib_vote_msb;
	/**< Upper 32bits of instantaneous bandwidth vote in bytes */

	u32 Sleep_latency;
	/**< Sleep latency vote in micro-seconds */

};

struct sysmon_smem_q6_event_stats_ext {

	int HMX_Power_state;
	/**< HMX Power state 1 : HMX ON
	 *                   0 : HMX OFF
	 *                  -1 : not supported
	 */

	int HMX_clk;
	/**< HMX clock in KHz, -1 When not supported */
};

struct sysmon_smem_q6_event_stats_extended {
	u32 featureId_q6Event;
	/**< Feature [31:28] Size [27:16] version[15:0] */

	struct sysmon_smem_q6_event_stats_local event_stats;
	/*Structure type to hold DSP Q6 event based statistics*/

	uint32_t Last_update_time_event_lsb;
	/**< LSB of Smem update lsb timestamp for event based stat updates*/

	uint32_t Last_update_time_event_msb;
	/**< MSB of Smem updatemsb timestamp for event based stat updates*/

	struct sysmon_smem_q6_event_stats_ext event_stats_ext;
	/**< Extended DSP Q6 event stats */
};

struct sysmon_smem_ddr_stats_local {
	uint32_t clk_arr[SYSMON_DDR_STATS_MAX_CLK_LEVELS];
	/**< DDR clock frequency(kHz) array */

	uint32_t active_time[SYSMON_DDR_STATS_MAX_CLK_LEVELS];
	/**< Active time(seconds) array correspons to DDR clock array */

	uint32_t num_ddrclk_levels;
	/**< Number of levels present in DDR clock table */

	uint32_t pc_time;
	/**< DSP Power collapse time(seconds) */

	uint32_t curr_clk;
	/**< Current DDR clock frequency(kHz) */

	uint32_t reserved;
	/**< Reserved field for future use */

	uint64_t checksum;
	/**< Check sum */
};

struct sysmon_smem_ddr_stats_extended {
	u32 featureId_ddrStats;
	/**< FeatureID[31:28] Size[27:16] version[15:0] */

	u32 reserved;
	/**< For structure alignment */

	struct sysmon_smem_ddr_stats_local stats;
	/**< Structure to hold DDR stats */

	uint32_t Last_update_time_event_lsb;
	/**< LSB of Smem update lsb timestamp for event based stat updates*/

	uint32_t Last_update_time_event_msb;
	/**< MSB of Smem updatemsb timestamp for event based stat updates*/
};


/**
 * struct smem_bw_stats_t - Bandwidth monitoring statistics (payload)
 *
 * @bw_sample_in_kbps:
 *      Latest computed average bandwidth sample, expressed in kilobits per second (kbps).
 *
 * @sampling_window_in_ms:
 *      Duration of the sampling window (in milliseconds) over which the BW_MON counter
 *      is averaged to produce @bw_sample_in_kbps.
 *
 * @accum_bw_mon_val:
 *      Total accumulated bandwidth/count in bytes since the start of accumulation
 *      (monotonically increasing until reset/overflow handling).
 */
struct smem_bw_stats_t {
	u32 bw_sample_in_kbps;

	u32 sampling_window_in_ms;

	u64 accum_bw_mon_val;
};

/**
 * struct sysmon_smem_bw_stats_extended - SMEM layout for bandwidth stats feature
 *
 * @featureId_bw_stats:
 *      Feature header encoding:
 *        - Feature ID in bits [31:28]
 *        - Feature size in bits [27:16]
 *        - Version in bits [15:0]
 *
 * @reserved:
 *      Reserved/padding field to maintain 8-byte alignment for subsequent 64-bit members.
 *
 * @Last_update_time_bwStats_lsb:
 *      Lower 32 bits (LSB) of the SMEM update timestamp for the bandwidth stats feature.
 *
 * @Last_update_time_bwStats_msb:
 *      Upper 32 bits (MSB) of the SMEM update timestamp for the bandwidth stats feature.
 *
 * @bw_stats:
 *      Bandwidth statistics payload (see struct smem_bw_stats_t).
 */
struct sysmon_smem_bw_stats_extended {
	u32 featureId_bw_stats;

	u32 reserved;

	u32 Last_update_time_bwStats_lsb;

	u32 Last_update_time_bwStats_msb;

	struct smem_bw_stats_t bw_stats;
};

/**
 * struct sysmon_smem_hmx_power_stats_t - SMEM layout for HMX power stats feature
 *
 * @clk_arr:
 *      HMX clock frequency(KHz) array.
 *
 * @on_time:
 *      HMX Power ON time(us) array corresponding to core clock array.
 *
 * @off_time:
 *      HMX Power OFF time(us).
 *
 * @num_hmxclk_levels:
 *      Number of levels present in HMX clock table.
 *
 * @current_clk:
 *      Current HMX clock frequency(KHz).
 *
 * @current_hmx_state:
 *      Current HMX State Power ON/OFF.
 *
 * @reserved:
 *      Reserved/padding field to maintain 8-byte alignment for subsequent 64-bit members.
 *
 * @check_sum:
 *      Sum of all the structure fileds in the current structure.
 */
struct sysmon_smem_hmx_power_stats_t {
	uint32_t clk_arr[SYSMON_POWER_STATS_MAX_CLK_LEVELS];

	uint64_t on_time[SYSMON_POWER_STATS_MAX_CLK_LEVELS];

	uint64_t off_time;

	uint32_t num_hmxclk_levels;

	uint32_t current_clk;

	uint32_t current_hmx_state;

	uint32_t reserved;

	uint64_t check_sum;
};

/**
 * struct sysmon_smem_hmx_power_stats_extended - SMEM layout for HMX power stats feature
 *
 * @featureId_HMXpowerStats:
 *      Feature header encoding:
 *        - Feature ID in bits [31:28]
 *        - Feature size in bits [27:16]
 *        - Version in bits [15:0]
 *
 * @sysmon_smem_hmx_power_stats:
 *      HMX power statistics payload (see struct sysmon_smem_hmx_power_stats_t).
 *
 * @Last_update_time_hmx_powerStats_lsb:
 *      Lower 32 bits (LSB) of the SMEM update timestamp for HMX power stats.
 *
 * @Last_update_time_hmx_powerStats_msb:
 *      Upper 32 bits (MSB) of the SMEM update timestamp for HMX power stats.
 */
struct sysmon_smem_hmx_power_stats_extended {
	uint32_t featureId_HMXpowerStats;

	struct sysmon_smem_hmx_power_stats_t sysmon_smem_hmx_power_stats;

	uint32_t Last_update_time_hmx_powerStats_lsb;

	uint32_t Last_update_time_hmx_powerStats_msb;
};

struct sysmon_smem_stats {
	bool smem_init_adsp;
	bool smem_init_cdsp;
	bool smem_init_slpi;
	bool smem_init_cdsp_hvx_util;
	bool smem_init_cdsp_hmx_util;
	bool smem_init_cdsp_hlx_util;
	bool smem_init_cdsp_bw_stats;
	bool smem_init_cdsp_ddr_stats;
	bool smem_init_cdsp_hmx_pwr_stats;
	struct sysmon_smem_power_stats_extended *sysmon_power_stats_adsp;
	struct sysmon_smem_power_stats_extended *sysmon_power_stats_cdsp;
	struct sysmon_smem_power_stats_extended *sysmon_power_stats_slpi;
	struct sysmon_smem_q6_event_stats_extended *sysmon_event_stats_adsp;
	struct sysmon_smem_q6_event_stats_extended *sysmon_event_stats_cdsp;
	struct sysmon_smem_q6_event_stats_extended *sysmon_event_stats_slpi;
	struct sysmon_smem_ddr_stats_extended *sysmon_ddr_stats;
	struct sysmon_smem_bw_stats_extended *sysmon_bw_stats;
	struct sysmon_smem_hmx_power_stats_extended *sysmon_hmx_power_stats;
	struct sleep_stats *sleep_stats_adsp;
	struct sleep_stats *sleep_stats_cdsp;
	struct sleep_stats *sleep_stats_slpi;
	struct sleep_stats_island *sleep_lpi_adsp;
	struct sleep_stats_island *sleep_lpi_slpi;
	u32 *q6_avg_load_adsp;
	u32 *q6_avg_load_cdsp;
	u32 *q6_avg_load_slpi;
	struct dsppm_stats *dsppm_stats_adsp;
	struct dsppm_stats *dsppm_stats_cdsp;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_master_adsp_stats;
	struct dentry *debugfs_master_cdsp_stats;
	u32 *hmx_util;
	u32 *hvx_util;
	u32 *hlx_util;
};

enum feature_id {
	SYSMONSTATS_Q6_LOAD_FEATUREID = 1,
	SYSMONSTATS_Q6_EVENT_FEATUREID = 2,
	SYSMON_POWER_STATS_FEATUREID = 3,
	SYSMON_HMX_UTIL_FEATUREID = 4,
	SYSMON_HVX_UTIL_FEATUREID = 5,
	SYSMON_RUNNING_STATS_FEATUREID = 6,
	SYSMON_HMX_POWER_STATS_FEATUREID = 7,
	SYSMON_DDR_POWER_STATS_FEATUREID = 8,
	SYSMON_HLX_UTIL_FEATUREID = 9,
	SYSMON_BW_STATS_FEATUREID = 10
};

struct sysmon_smem_q6_load_stats {
	u32 q6load_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

struct sysmon_smem_hvx_stats {
	u32 hvx_util_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

struct sysmon_smem_hmx_stats {
	u32 hmx_util_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

struct sysmon_smem_hlx_stats {
	u32 hlx_util_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

/**
 * Structure to hold DSP-specific sleep stats pointers and initialization function
 */
struct dsp_sleep_config {
	bool *smem_init_flag;
	void (*init_func)(void);
	struct sleep_stats *lpm_stats;
	struct sleep_stats_island *lpi_stats;
};

enum sub_id {
	HMX_CDSP,
	DDR_CDSP
};
struct delta_time {
	uint32_t active_time;
	uint32_t pc_time;
	uint32_t lpi_time;
};
static struct sysmon_smem_stats g_sysmon_stats;

static int get_delta_time(enum sub_id sub_id, struct delta_time *delta_time,
	u64 last_updated_time, u32 pc_time_updated, u32 lpi_time_updated)
{
	u64 lpm_acc = 0, lpi_acc = 0;
	u64 updated_ticks = 0, curr_timestamp = 0, diff_ticks = 0;
	u64 pc_time = 0, lpi_time = 0, active_time = 0, diff_time = 0;

	if (delta_time == NULL)
		return -EINVAL;

	if (sub_id == DDR_CDSP) {
		if (g_sysmon_stats.sleep_stats_cdsp) {
			lpm_acc = g_sysmon_stats.sleep_stats_cdsp->accumulated;
			if (g_sysmon_stats.sleep_stats_cdsp->last_entered_at >
					g_sysmon_stats.sleep_stats_cdsp->last_exited_at)
				lpm_acc += arch_timer_read_counter() -
					g_sysmon_stats.sleep_stats_cdsp->last_entered_at;
		} else
			return -EINVAL;
	}
	updated_ticks = (last_updated_time);
	curr_timestamp = __arch_counter_get_cntvct();

	if (curr_timestamp < updated_ticks)
		diff_ticks = curr_timestamp + (ULLONG_MAX - updated_ticks);
	else
		diff_ticks = (curr_timestamp - updated_ticks);

	diff_time = (diff_ticks / SYS_CLK_TICKS_PER_SEC);
	if (diff_time > 0) {
		pc_time = (lpm_acc / SYS_CLK_TICKS_PER_SEC);
		lpi_time = (lpi_acc / SYS_CLK_TICKS_PER_SEC);

		if (pc_time >= pc_time_updated)
			pc_time -= pc_time_updated;

		if (diff_time >= pc_time)
			active_time = diff_time - pc_time;

		if ((lpi_time) && (lpi_time >= lpi_time_updated))
			lpi_time -= lpi_time_updated;

		if ((lpi_time) && (pc_time >= lpi_time))
			pc_time -= lpi_time;
	}
	delta_time->active_time = active_time;
	delta_time->pc_time = pc_time;
	delta_time->lpi_time = lpi_time;
	return 0;
}

/* Adds delta between SMEM powerstats updated time to current time */
static int add_delta_time(
	u8 ver,
	u64 lpi_acc, u64 lpm_acc,
	struct sysmon_smem_power_stats *stats, enum dsp_id_t dsp_id
)
{
	u64 powerstats_ticks = 0, curr_timestamp = 0, diff_ticks = 0;
	u64 pc_time = 0, lpi_time = 0, active_time = 0, diff_time = 0;
	u8 j = 0;
	struct sysmon_smem_power_stats_extended *ptr = NULL;

	if (dsp_id == ADSP) {
		ptr = g_sysmon_stats.sysmon_power_stats_adsp;
	} else if (dsp_id == CDSP) {
		ptr = g_sysmon_stats.sysmon_power_stats_cdsp;
	} else if (dsp_id == SLPI) {
		ptr = g_sysmon_stats.sysmon_power_stats_slpi;
	} else {
		return -EINVAL;
	}

	if (ptr == NULL)
		return -EINVAL;

	if (ver >= 2) {
		powerstats_ticks = (u64)(((u64)ptr->last_update_time_powerstats_msb << 32) |
							ptr->last_update_time_powerstats_lsb);
		curr_timestamp = __arch_counter_get_cntvct();

		if (curr_timestamp < powerstats_ticks)
			diff_ticks = curr_timestamp + (ULLONG_MAX - powerstats_ticks);
		else
			diff_ticks = (curr_timestamp - powerstats_ticks);

		diff_time = (diff_ticks / SYS_CLK_TICKS_PER_SEC);

		if (diff_time > 0) {
			pc_time = (lpm_acc / SYS_CLK_TICKS_PER_SEC);
			lpi_time = (lpi_acc / SYS_CLK_TICKS_PER_SEC);

			if (pc_time >= stats->pc_time)
				pc_time -= stats->pc_time;

			if (diff_time >= pc_time)
				active_time = diff_time - pc_time;

			if ((lpi_time) && (lpi_time >= stats->lpi_time))
				lpi_time -= stats->lpi_time;

			if ((lpi_time) && (pc_time >= lpi_time))
				pc_time -= lpi_time;

			for (j = 0; j < SYSMON_POWER_STATS_MAX_CLK_LEVELS; j++) {
				if (stats->clk_arr[j] == stats->current_clk) {
					stats->active_time[j] += active_time;
					stats->island_time[j] += lpi_time;
					break;
				}
			}

			if (j >= SYSMON_POWER_STATS_MAX_CLK_LEVELS) {
				pr_err("%s: Current clock level %u KHz didn't match with any clock level\n",
					__func__, stats->current_clk);
				return -EINVAL;
			}
			stats->pc_time += pc_time;
			stats->lpi_time += lpi_time;
		}
	}

	return 0;
}

/*Power Stats helper function*/
static void handle_power_stats_feature(
	void *smem_pointer, int size_of_u32,
	enum dsp_id_t dsp_id,
	size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
		if (dsp_id == ADSP)
			g_sysmon_stats.sysmon_power_stats_adsp =
			(struct sysmon_smem_power_stats_extended *)
			(smem_pointer);
		else if (dsp_id == CDSP)
			g_sysmon_stats.sysmon_power_stats_cdsp =
			(struct sysmon_smem_power_stats_extended *)
			(smem_pointer);
		else if (dsp_id == SLPI)
			g_sysmon_stats.sysmon_power_stats_slpi =
			(struct sysmon_smem_power_stats_extended *)
			(smem_pointer);
		else
			pr_err("%s: subsystem not found %d\n",
				__func__, SYSMON_POWER_STATS_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_POWER_STATS_FEATUREID);
		*size = 0;
	}
}

/*Q6 Event helper function*/
static void handle_q6_event_feature(
	void *smem_pointer, int size_of_u32,
	enum dsp_id_t dsp_id, size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
		if (dsp_id == ADSP)
			g_sysmon_stats.sysmon_event_stats_adsp =
			(struct sysmon_smem_q6_event_stats_extended *)
			(smem_pointer);
		else if (dsp_id == CDSP)
			g_sysmon_stats.sysmon_event_stats_cdsp =
			(struct sysmon_smem_q6_event_stats_extended *)
			(smem_pointer);
		else if (dsp_id == SLPI)
			g_sysmon_stats.sysmon_event_stats_slpi =
			(struct sysmon_smem_q6_event_stats_extended *)
			(smem_pointer);
		else
			pr_err("%s:subsystem not found %d\n",
			__func__, SYSMONSTATS_Q6_EVENT_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMONSTATS_Q6_EVENT_FEATUREID);
		*size = 0;
	}
}

/*Q6 Load helper function*/
static void handle_q6_load_feature(
	void *smem_pointer, int size_of_u32,
	enum dsp_id_t dsp_id, size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
		if (dsp_id == ADSP)
			g_sysmon_stats.q6_avg_load_adsp =
			(u32 *)(smem_pointer + size_of_u32);
		else if (dsp_id == CDSP)
			g_sysmon_stats.q6_avg_load_cdsp =
			(u32 *)(smem_pointer + size_of_u32);
		else if (dsp_id == SLPI)
			g_sysmon_stats.q6_avg_load_slpi =
			(u32 *)(smem_pointer + size_of_u32);
		else
			pr_err("%s:subsystem not found %d\n",
				__func__, SYSMONSTATS_Q6_LOAD_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMONSTATS_Q6_LOAD_FEATUREID);
		*size = 0;
	}
}

/*HMX Util helper function*/
static void handle_hmx_util_feature(
	void *smem_pointer, int size_of_u32,
	enum dsp_id_t dsp_id, size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
		if (dsp_id == CDSP) {
			g_sysmon_stats.hmx_util =
			(u32 *)(smem_pointer + size_of_u32);
		} else
			pr_err("%s:subsystem not supported %d\n",
				__func__, SYSMON_HMX_UTIL_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_HMX_UTIL_FEATUREID);
		*size = 0;
	}
}

/*HVX Util helper function*/
static void handle_hvx_util_feature(
	void *smem_pointer, int size_of_u32,
	enum dsp_id_t dsp_id, size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
		if (dsp_id == CDSP) {
			g_sysmon_stats.hvx_util =
			(u32 *)(smem_pointer + size_of_u32);
		} else
			pr_err("%s:subsystem not supported %d\n",
				__func__, SYSMON_HVX_UTIL_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_HVX_UTIL_FEATUREID);
		*size = 0;
	}
}

/*HLX Util helper function*/
static void handle_hlx_util_feature(
	void *smem_pointer, int size_of_u32,
	enum dsp_id_t dsp_id, size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
		if (dsp_id == CDSP) {
			g_sysmon_stats.hlx_util =
			(u32 *)(smem_pointer + size_of_u32);
		} else
			pr_err("%s:subsystem not supported %d\n",
				__func__, SYSMON_HLX_UTIL_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_HLX_UTIL_FEATUREID);
		*size = 0;
	}
}

/*DDR Stats helper function*/
static void handle_ddr_stats_feature(
	void *smem_pointer, enum dsp_id_t dsp_id,
	size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer)) {
		if (dsp_id == CDSP) {
			g_sysmon_stats.sysmon_ddr_stats = smem_pointer;
		} else
			pr_err("%s:subsystem not supported %d stats\n",
				__func__, SYSMON_DDR_POWER_STATS_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_DDR_POWER_STATS_FEATUREID);
		*size = 0;
	}
}

/* BandWidth Stats helper function */
static void handle_bw_stats_feature(
	void *smem_pointer, enum dsp_id_t dsp_id,
	size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer)) {
		if (dsp_id == CDSP)
			g_sysmon_stats.sysmon_bw_stats = smem_pointer;
		else
			pr_err("%s:subsystem not supported %d stats\n",
				__func__, SYSMON_BW_STATS_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_BW_STATS_FEATUREID);
		*size = 0;
	}
}

/* HMX power Stats helper function */
static void handle_hmx_power_stats_feature(
	void *smem_pointer, enum dsp_id_t dsp_id,
	size_t *size
)
{
	if (!IS_ERR_OR_NULL(smem_pointer)) {
		if (dsp_id == CDSP)
			g_sysmon_stats.sysmon_hmx_power_stats = smem_pointer;
		else
			pr_err("%s:subsystem not supported %d stats\n",
				__func__, SYSMON_HMX_POWER_STATS_FEATUREID);
	} else {
		pr_err("%s: Failed to fetch %d feature pointer\n",
			__func__, SYSMON_HMX_POWER_STATS_FEATUREID);
		*size = 0;
	}
}

/*Updates SMEM pointers for all the sysmon Master stats*/
static void update_sysmon_smem_pointers(void *smem_pointer, enum dsp_id_t dsp_id, size_t size)
{
	u32 featureId;
	int feature, size_rcvd;
	int size_of_u32 = sizeof(u32);

	featureId = *(unsigned int *)smem_pointer;
	feature = featureId >> 28;
	size_rcvd = (featureId >> 16) & 0xFFF;

	while ((size > 0) && (size >= size_rcvd)) {
		switch (feature) {
		case SYSMON_POWER_STATS_FEATUREID:
			handle_power_stats_feature(smem_pointer, size_of_u32, dsp_id, &size);
		break;
		case SYSMONSTATS_Q6_EVENT_FEATUREID:
			handle_q6_event_feature(smem_pointer, size_of_u32, dsp_id, &size);
		break;
		case SYSMONSTATS_Q6_LOAD_FEATUREID:
			handle_q6_load_feature(smem_pointer, size_of_u32, dsp_id, &size);
		break;
		case SYSMON_HMX_UTIL_FEATUREID:
			handle_hmx_util_feature(smem_pointer, size_of_u32, dsp_id, &size);
		break;
		case SYSMON_HVX_UTIL_FEATUREID:
			handle_hvx_util_feature(smem_pointer, size_of_u32, dsp_id, &size);
		break;
		case SYSMON_HLX_UTIL_FEATUREID:
			handle_hlx_util_feature(smem_pointer, size_of_u32, dsp_id, &size);
		break;
		case SYSMON_DDR_POWER_STATS_FEATUREID:
			handle_ddr_stats_feature(smem_pointer, dsp_id, &size);
		break;
		case SYSMON_BW_STATS_FEATUREID:
			handle_bw_stats_feature(smem_pointer, dsp_id, &size);
		break;
		case SYSMON_HMX_POWER_STATS_FEATUREID:
			handle_hmx_power_stats_feature(smem_pointer, dsp_id, &size);
		break;
		default:
			pr_err("%s: Requested feature not found, Feature:%u\n",
					__func__, feature);
		break;
		}
		if (!IS_ERR_OR_NULL(smem_pointer + size_rcvd)
					&& (size > size_rcvd)) {
			featureId = *(unsigned int *)(smem_pointer + size_rcvd);
			smem_pointer += size_rcvd;
			size = size - size_rcvd;
			feature = featureId >> 28;
			size_rcvd = (featureId >> 16) & 0xFFF;
		} else {
			size = 0;
		}
	}
}

static void sysmon_smem_init_adsp(void)
{
	size_t size;
	void *smem_pointer_adsp = NULL;

	g_sysmon_stats.smem_init_adsp = true;

	g_sysmon_stats.dsppm_stats_adsp = qcom_smem_get(ADSP,
						DSPPMSTATS_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.dsppm_stats_adsp) ||
		(sizeof(struct dsppm_stats) > size)) {
		pr_err("%s:Failed to get fetch dsppm stats from SMEM for ADSP: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.dsppm_stats_adsp), size);
		g_sysmon_stats.smem_init_adsp = false;
	}

	g_sysmon_stats.sleep_stats_adsp = qcom_smem_get(ADSP,
						SLEEPSTATS_SMEM_ID_ADSP,
						&size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_adsp) ||
		(sizeof(struct sleep_stats) > size)) {
		pr_err("%s:Failed to get fetch sleep data from SMEM for ADSP: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_stats_adsp), size);
		g_sysmon_stats.smem_init_adsp = false;
	}

	g_sysmon_stats.sleep_lpi_adsp = qcom_smem_get(ADSP,
						SLEEPSTATS_LPI_SMEM_ID,
						&size);
	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_lpi_adsp) ||
		(sizeof(struct sleep_stats_island) > size)) {
		pr_err("%s:Failed to get fetch LPI sleep data from SMEM for ADSP: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_lpi_adsp), size);
		g_sysmon_stats.smem_init_adsp = false;
	}

	smem_pointer_adsp = qcom_smem_get(ADSP,
						SYSMON_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(smem_pointer_adsp) || !size) {
		pr_err("%s:Failed to get fetch sysmon data from SMEM for ADSP: %ld, size: %lu\n",
				__func__, PTR_ERR(smem_pointer_adsp), size);
		g_sysmon_stats.smem_init_adsp = false;
	}

	update_sysmon_smem_pointers(smem_pointer_adsp, ADSP, size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_adsp)) {

		pr_err("%s:Failed to get stats from SMEM for ADSP:\n"
				"event stats:%lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_event_stats_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_adsp)) {

		pr_err("%s:Failed to get stats from SMEM for ADSP:\n"
				"power stats: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_power_stats_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_adsp)) {

		pr_err("%s:Failed to get stats from SMEM for ADSP:\n"
				"q6_avg_load: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.q6_avg_load_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}
}

static void sysmon_smem_init_cdsp(void)
{
	size_t size;
	void *smem_pointer_cdsp = NULL;

	g_sysmon_stats.smem_init_cdsp = true;
	g_sysmon_stats.smem_init_cdsp_hvx_util = true;
	g_sysmon_stats.smem_init_cdsp_hmx_util = true;
	g_sysmon_stats.smem_init_cdsp_hlx_util = true;
	g_sysmon_stats.smem_init_cdsp_ddr_stats = true;
	g_sysmon_stats.smem_init_cdsp_bw_stats = true;
	g_sysmon_stats.smem_init_cdsp_hmx_pwr_stats = true;
	g_sysmon_stats.dsppm_stats_cdsp = qcom_smem_get(CDSP,
						DSPPMSTATS_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.dsppm_stats_cdsp) ||
		(sizeof(struct dsppm_stats) > size)) {
		pr_err("%s:Failed to get fetch dsppm stats from SMEM for CDSP: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.dsppm_stats_cdsp), size);
		g_sysmon_stats.smem_init_cdsp = false;
	}

	g_sysmon_stats.sleep_stats_cdsp = qcom_smem_get(CDSP,
						SLEEPSTATS_SMEM_ID_CDSP,
						&size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_cdsp) ||
		(sizeof(struct sleep_stats) > size)) {
		pr_err("%s:Failed to get fetch sleep data from SMEM for CDSP: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_stats_cdsp), size);
		g_sysmon_stats.smem_init_cdsp = false;
	}

	smem_pointer_cdsp = qcom_smem_get(CDSP,
							SYSMON_SMEM_ID,
							&size);
	if (IS_ERR_OR_NULL(smem_pointer_cdsp) || !size) {
		pr_err("%s:Failed to get fetch data from SMEM for CDSP: %ld, size: %lu\n",
				__func__, PTR_ERR(smem_pointer_cdsp), size);
		g_sysmon_stats.smem_init_cdsp = false;
	}

	update_sysmon_smem_pointers(smem_pointer_cdsp, CDSP, size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_cdsp)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"event stats:%lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_event_stats_cdsp));
		g_sysmon_stats.smem_init_cdsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_cdsp)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				" power stats: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_power_stats_cdsp));
		g_sysmon_stats.smem_init_cdsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_cdsp)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"q6_avg_load: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.q6_avg_load_cdsp));
		g_sysmon_stats.smem_init_cdsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.hmx_util)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"hmx_util: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.hmx_util));
		g_sysmon_stats.smem_init_cdsp_hmx_util = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.hvx_util)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"hvx_util: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.hvx_util));
		g_sysmon_stats.smem_init_cdsp_hvx_util = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.hlx_util)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"hlx_util: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.hlx_util));
		g_sysmon_stats.smem_init_cdsp_hlx_util = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_bw_stats)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"sysmon_bw_stats: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_bw_stats));
		g_sysmon_stats.smem_init_cdsp_bw_stats = false;
		/* Note: BW stats are optional, not setting smem_init_cdsp to false */
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_ddr_stats)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"sysmon_ddr_stats: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_ddr_stats));
		g_sysmon_stats.smem_init_cdsp_ddr_stats = false;
		/* Note: DDR stats are optional, not affecting overall CDSP init status */
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_hmx_power_stats)) {
		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"sysmon_hmx_power_stats: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_hmx_power_stats));
		g_sysmon_stats.smem_init_cdsp_hmx_pwr_stats = false;
	}
}

static void sysmon_smem_init_slpi(void)
{
	size_t size;
	void *smem_pointer_slpi = NULL;

	g_sysmon_stats.smem_init_slpi = true;

	g_sysmon_stats.sleep_stats_slpi = qcom_smem_get(SLPI,
						SLEEPSTATS_SMEM_ID_SLPI,
						&size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_slpi) ||
		(sizeof(struct sleep_stats) > size)) {
		pr_err("%s:Failed to get fetch sleep data from SMEM for SLPI: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_stats_slpi), size);
		g_sysmon_stats.smem_init_slpi = false;
	}

	g_sysmon_stats.sleep_lpi_slpi = qcom_smem_get(SLPI,
						SLEEPSTATS_LPI_SMEM_ID,
						&size);
	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_lpi_slpi) ||
		(sizeof(struct sleep_stats_island) > size)) {
		pr_err("%s:Failed to get fetch LPI sleep data from SMEM for SLPI: %ld, size: %lu\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_lpi_slpi), size);
		g_sysmon_stats.smem_init_slpi = false;
	}

	smem_pointer_slpi = qcom_smem_get(SLPI,
							SYSMON_SMEM_ID,
							&size);

	if (IS_ERR_OR_NULL(smem_pointer_slpi) || !size) {
		pr_err("%s:Failed to get fetch data from SMEM for SLPI: %ld, size: %lu\n",
				__func__, PTR_ERR(smem_pointer_slpi), size);
	}

	update_sysmon_smem_pointers(smem_pointer_slpi, SLPI, size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_slpi)) {

		pr_err("%s:Failed to get stats from SMEM for SLPI:\n"
				"event stats:%lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_event_stats_slpi));
		g_sysmon_stats.smem_init_slpi = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_slpi)) {

		pr_err("%s:Failed to get stats from SMEM for SLPI:\n"
				"power stats: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.sysmon_power_stats_slpi));
		g_sysmon_stats.smem_init_slpi = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_slpi)) {

		pr_err("%s:Failed to get stats from SMEM for SLPI:\n"
				"q6_avg_load: %lx\n",
				__func__, PTR_ERR(g_sysmon_stats.q6_avg_load_slpi));
		g_sysmon_stats.smem_init_slpi = false;
	}
}
/**
 * sysmon_read_hmx_util() - Checks for CDSP power collapse
 * and resets the HMX utilization to zero if dsp is power collapsed.
 */
static u32 sysmon_read_hmx_util(void)
{
	struct sysmon_smem_hmx_stats sysmon_hmx_util;
	u64 curr_timestamp = __arch_counter_get_cntvct();
	u64 last_hmx_update_at = 0;

	memcpy(&sysmon_hmx_util,
		g_sysmon_stats.hmx_util,
		sizeof(struct sysmon_smem_hmx_stats));

	last_hmx_update_at =
		(u64) (((u64)sysmon_hmx_util.Last_update_time_load_msb << 32)|
				sysmon_hmx_util.Last_update_time_load_lsb);

	if ((curr_timestamp > last_hmx_update_at) &&
		((curr_timestamp - last_hmx_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
		sysmon_hmx_util.hmx_util_avg = 0;
	}

	if (sysmon_hmx_util.hmx_util_avg == HMX_HVX_PMU_EVENTS_NA)
		pr_err("HMX PMU not registered, user ovveride pmu counter\n");

	return sysmon_hmx_util.hmx_util_avg;
}

/**
 * sysmon_stats_query_hmx_utlization() - * API to query
 * CDSP hmx utlization.On success, returns HMX utilization
 * in the hmx_util parameter.
 * @arg1: u32 pointer to HMX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hmx_utlization(u32 *hmx_util)
{
	u32 hmx_utilization = 0;
	int ret = 0;

	if (!g_sysmon_stats.smem_init_cdsp_hmx_util)
		sysmon_smem_init_cdsp();

	if (!IS_ERR_OR_NULL(g_sysmon_stats.hmx_util)) {
		hmx_utilization = sysmon_read_hmx_util();

		if (hmx_utilization == HMX_HVX_PMU_EVENTS_NA) {
			hmx_utilization = 0;
			ret = DSP_PMU_COUNTER_NA;
		}

		memcpy(hmx_util, &hmx_utilization, sizeof(u32));
	} else
		return -ENOKEY;

	return ret;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_hmx_utlization);

/**
 * sysmon_read_hlx_util() - Checks for CDSP power collapse
 * and resets the HLX utilization to zero if dsp is power collapsed.
 */
static u32 sysmon_read_hlx_util(void)
{
	struct sysmon_smem_hlx_stats sysmon_hlx_util;
	u64 curr_timestamp = __arch_counter_get_cntvct();
	u64 last_hlx_update_at = 0;

	memcpy(&sysmon_hlx_util,
		g_sysmon_stats.hlx_util,
		sizeof(struct sysmon_smem_hlx_stats));

	last_hlx_update_at =
		(u64) (((u64)sysmon_hlx_util.Last_update_time_load_msb << 32)|
				sysmon_hlx_util.Last_update_time_load_lsb);

	if ((curr_timestamp > last_hlx_update_at) &&
		((curr_timestamp - last_hlx_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
		sysmon_hlx_util.hlx_util_avg = 0;
	}

	if (sysmon_hlx_util.hlx_util_avg == HLX_PMU_EVENTS_NA)
		pr_err("HLX PMU not registered, user ovveride pmu counter\n");

	return sysmon_hlx_util.hlx_util_avg;
}

/**
 * sysmon_stats_query_hlx_utilization() - * API to query
 * CDSP hlx utlization.On success, returns HLX utilization
 * in the hlx_util parameter.
 * @arg1: u32 pointer to HLX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hlx_utilization(u32 *hlx_util)
{
	u32 hlx_utilization = 0;
	int ret = 0;

	if (!g_sysmon_stats.smem_init_cdsp_hlx_util)
		sysmon_smem_init_cdsp();

	if (!IS_ERR_OR_NULL(g_sysmon_stats.hlx_util)) {
		hlx_utilization = sysmon_read_hlx_util();

		if (hlx_utilization == HLX_PMU_EVENTS_NA) {
			hlx_utilization = 0;
			ret = DSP_PMU_COUNTER_NA;
		}

		memcpy(hlx_util, &hlx_utilization, sizeof(u32));
	} else
		return -ENOKEY;

	return ret;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_hlx_utilization);

/**
 * sysmon_read_hvx_util() - Checks for CDSP power collapse
 * and resets the HVX utilization to zero if dsp is power collapsed.
 */
static u32 sysmon_read_hvx_util(void)
{
	struct sysmon_smem_hvx_stats sysmon_hvx_util;
	u64 curr_timestamp = __arch_counter_get_cntvct();
	u64 last_hvx_update_at = 0;

	memcpy(&sysmon_hvx_util,
		g_sysmon_stats.hvx_util,
		sizeof(struct sysmon_smem_hvx_stats));

	last_hvx_update_at =
		(u64) (((u64)sysmon_hvx_util.Last_update_time_load_msb << 32)|
				sysmon_hvx_util.Last_update_time_load_lsb);

	if ((curr_timestamp > last_hvx_update_at) &&
		((curr_timestamp - last_hvx_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
		sysmon_hvx_util.hvx_util_avg = 0;
	}

	if (sysmon_hvx_util.hvx_util_avg == HMX_HVX_PMU_EVENTS_NA)
		pr_err("HVX PMU not registered, user ovveride pmu counter\n");

	return sysmon_hvx_util.hvx_util_avg;
}

/**
 * sysmon_read_bw_stats() - API to read CDSP BandWidth statistics.
 * It will return current BandWidth and accumulated BandWidth along
 * with timestamp when the stats were updated in smem.
 * It will update current BandWidth as 0 if there is no active
 * BandWidth in last 100ms.
 * @arg1: smem_bw_stats_ioctl pointer to BandWidth stats.
 */
static int sysmon_read_bw_stats(struct smem_bw_stats_ioctl *bw_stats_ioctl)
{
	u64 last_bw_update_at = 0;
	u64 curr_timestamp = 0;

	bw_stats_ioctl->bw_stats_driver_capability_mask = BWMON_STATS_DEFAULT_FEATURE_MASK;
	bw_stats_ioctl->bw_sample_in_kbps =
				g_sysmon_stats.sysmon_bw_stats->bw_stats.bw_sample_in_kbps;
	bw_stats_ioctl->sampling_window_in_ms =
				g_sysmon_stats.sysmon_bw_stats->bw_stats.sampling_window_in_ms;
	bw_stats_ioctl->accum_bw_mon_val =
				g_sysmon_stats.sysmon_bw_stats->bw_stats.accum_bw_mon_val;

	last_bw_update_at =
		(u64) (((u64)g_sysmon_stats.sysmon_bw_stats->Last_update_time_bwStats_msb << 32)|
				g_sysmon_stats.sysmon_bw_stats->Last_update_time_bwStats_lsb);

	bw_stats_ioctl->last_updated_timestamp = last_bw_update_at;
	curr_timestamp = __arch_counter_get_cntvct();

	bw_stats_ioctl->current_timestamp = curr_timestamp;

	if ((curr_timestamp > last_bw_update_at) &&
		((curr_timestamp - last_bw_update_at) / SYS_CLK_TICKS_PER_MS) > 100)
		bw_stats_ioctl->bw_sample_in_kbps = 0;

	return 0;
}

/**
 * sysmon_stats_query_hvx_utlization() - * API to query
 * CDSP subsystem hvx utlization.On success, returns HVX utilization
 * in the hvx_util parameter.
 * @arg1: u32 pointer to HVX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hvx_utlization(u32 *hvx_util)
{
	u32 hvx_utilization = 0;
	int ret = 0;

	if (!g_sysmon_stats.smem_init_cdsp_hvx_util)
		sysmon_smem_init_cdsp();

	if (!IS_ERR_OR_NULL(g_sysmon_stats.hvx_util)) {
		hvx_utilization = sysmon_read_hvx_util();

		if (hvx_utilization == HMX_HVX_PMU_EVENTS_NA) {
			hvx_utilization = 0;
			ret = DSP_PMU_COUNTER_NA;
		}

		memcpy(hvx_util, &hvx_utilization, sizeof(u32));
	} else
		return -ENOKEY;

	return ret;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_hvx_utlization);

/**
 * sysmon_stats_query_bw_stats() - * API to get BandWidth stats
 * CDSP subsystem BandWidth statistics.On success, returns
 * BandWidth stats in the bw_stats parameter.
 * @arg1: smem_bw_stats_ioctl pointer to BandWidth statistics.
 * @return: SUCCESS (0) if API is successful
 *          FAILURE (Non-zero) if API failed, refer error codes.
 */
int sysmon_stats_query_bw_stats(struct smem_bw_stats_ioctl *bw_stats_ioctl)
{
	int ret = 0;

	if (!bw_stats_ioctl) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	if (!g_sysmon_stats.smem_init_cdsp_bw_stats)
		sysmon_smem_init_cdsp();

	if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_bw_stats))
		ret = sysmon_read_bw_stats(bw_stats_ioctl);
	else
		return -ENOKEY;

	return ret;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_bw_stats);

static int copy_powerstats(struct sysmon_smem_power_stats *out_ptr, enum dsp_id_t dsp_id)
{
	const struct sysmon_smem_power_stats_extended *in_ptr = NULL;
	int ver = 0;
	int i = 0, j = 0;

	if (dsp_id == ADSP)
		in_ptr = g_sysmon_stats.sysmon_power_stats_adsp;
	else if (dsp_id == CDSP)
		in_ptr = g_sysmon_stats.sysmon_power_stats_cdsp;
	else if (dsp_id == SLPI)
		in_ptr = g_sysmon_stats.sysmon_power_stats_slpi;
	else {
		pr_err("%s: Stats not found for dsp(%d)\n", __func__, dsp_id);
		return 1;
	}
	out_ptr->version = in_ptr->powerstats.version & 0xFF;
	ver = in_ptr->powerstats.version & 0xFF;
	out_ptr->pc_time = in_ptr->powerstats.pc_time;
	out_ptr->lpi_time = in_ptr->powerstats.lpi_time;
	if (ver >= 2)
		out_ptr->current_clk = in_ptr->powerstats.current_clk;

	for (i = 0; i < SYSMON_POWER_STATS_MAX_CLK_LEVELS_16; i++) {
		out_ptr->clk_arr[i] = in_ptr->powerstats.clk_arr[i];
		out_ptr->active_time[i] = in_ptr->powerstats.active_time[i];
		if ((ver >= 2) && ((dsp_id == ADSP) || (dsp_id == SLPI)))
			out_ptr->island_time[i] = in_ptr->powerstats.island_time[i];
	}
	if (ver >= 4) {
		for (i = SYSMON_POWER_STATS_MAX_CLK_LEVELS_16;
			i < SYSMON_POWER_STATS_MAX_CLK_LEVELS; i++) {
			j = i - SYSMON_POWER_STATS_MAX_CLK_LEVELS_16;
			out_ptr->clk_arr[i] = in_ptr->powerstats_ext.clk_arr[j];
			out_ptr->active_time[i] = in_ptr->powerstats_ext.active_time[j];
			if (dsp_id == ADSP)
				out_ptr->island_time[i] = in_ptr->powerstats_ext.island_time[j];
		}
	}
	return 0;
}

/**
 * sysmon_stats_query_ddr_residency() - * API to query requested
 * DDR power residency.
 * On success, returns 0 and copies power residency
 *      statistics in the given sysmon_smem_ddr_stats structure.
 */
int sysmon_stats_query_ddr_residency(struct sysmon_smem_ddr_stats *sysmon_ddr_stats)
{
	struct delta_time delta = {0};
	uint32_t dtime = 0, j = 0;
	struct sysmon_smem_ddr_stats_extended *stats = g_sysmon_stats.sysmon_ddr_stats;

	if (!sysmon_ddr_stats) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	if (!g_sysmon_stats.smem_init_cdsp_ddr_stats)
		sysmon_smem_init_cdsp();

	if (g_sysmon_stats.sysmon_ddr_stats) {
		if (get_delta_time(DDR_CDSP, &delta,
				(u64)(((u64)stats->Last_update_time_event_msb << 32)
				| stats->Last_update_time_event_lsb),
				stats->stats.pc_time, 0))
			return -ENOKEY;

		if (stats->stats.num_ddrclk_levels > SYSMON_DDR_STATS_MAX_CLK_LEVELS) {
			pr_err("%s: Number of DDR clock levels (%d) are more than expected %d\n",
				__func__, stats->stats.num_ddrclk_levels,
				SYSMON_DDR_STATS_MAX_CLK_LEVELS);
			return -EINVAL;
		}

		for (j = 0; j < stats->stats.num_ddrclk_levels; j++) {
			if (stats->stats.clk_arr[j]) {
				if (stats->stats.clk_arr[j] == stats->stats.curr_clk)
					dtime = delta.active_time;
				else
					dtime = 0;

				sysmon_ddr_stats->clk_arr[j] = stats->stats.clk_arr[j];
				sysmon_ddr_stats->active_time[j] =
						stats->stats.active_time[j] + dtime;
			}
		}
		sysmon_ddr_stats->pc_time = stats->stats.pc_time + delta.pc_time;
		sysmon_ddr_stats->curr_clk = stats->stats.curr_clk;
		sysmon_ddr_stats->num_ddrclk_levels = stats->stats.num_ddrclk_levels;
	} else
		return -ENOKEY;
	return 0;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_ddr_residency);

/**
 * sysmon_stats_query_cdsp_hmx_residency() - * API to query requested
 * HMX power residency.
 * @arg1: smem_hmx_power_stats pointer to HMX power residency statistics.
 * @return: SUCCESS (0) if API is successful
 *          FAILURE (Non-zero) if API failed, refer error codes.
 */
int sysmon_stats_query_cdsp_hmx_residency(struct smem_hmx_power_stats *sysmon_hmx_stats)
{
	uint32_t dtime = 0, j = 0;
	struct sysmon_smem_hmx_power_stats_extended *stats = g_sysmon_stats.sysmon_hmx_power_stats;
	u64 Last_updated_timestamp = 0, diff_ticks = 0, curr_timestamp = 0, diff_time = 0;

	if (!sysmon_hmx_stats) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	if (!g_sysmon_stats.smem_init_cdsp_hmx_pwr_stats)
		sysmon_smem_init_cdsp();

	if (g_sysmon_stats.sysmon_hmx_power_stats) {
		if (stats->sysmon_smem_hmx_power_stats.num_hmxclk_levels >
			SYSMON_POWER_STATS_MAX_CLK_LEVELS) {
			pr_err("%s: Number of HMX clock levels (%d) are more than expected %d\n",
				__func__, stats->sysmon_smem_hmx_power_stats.num_hmxclk_levels,
				SYSMON_POWER_STATS_MAX_CLK_LEVELS);
			return -EINVAL;
		}

		Last_updated_timestamp =
		(u64)(((u64)stats->Last_update_time_hmx_powerStats_msb << 32)
		| stats->Last_update_time_hmx_powerStats_lsb);
		curr_timestamp = __arch_counter_get_cntvct();

		if (curr_timestamp < Last_updated_timestamp)
			diff_ticks = curr_timestamp + (ULLONG_MAX - Last_updated_timestamp);
		else
			diff_ticks = (curr_timestamp - Last_updated_timestamp);

		diff_time = (diff_ticks / SYS_CLK_TICKS_PER_SEC);

		for (j = 0; j < stats->sysmon_smem_hmx_power_stats.num_hmxclk_levels; j++) {
			if (stats->sysmon_smem_hmx_power_stats.clk_arr[j]) {
				if ((stats->sysmon_smem_hmx_power_stats.clk_arr[j] ==
					stats->sysmon_smem_hmx_power_stats.current_clk) &&
					(stats->sysmon_smem_hmx_power_stats.current_hmx_state != 0))
					dtime = diff_time;
				else
					dtime = 0;

				sysmon_hmx_stats->clk_arr[j] =
				stats->sysmon_smem_hmx_power_stats.clk_arr[j];
				sysmon_hmx_stats->on_time[j] =
				(stats->sysmon_smem_hmx_power_stats.on_time[j]
				/ (1000*1000)) + dtime;
			}
		}

		if (stats->sysmon_smem_hmx_power_stats.current_hmx_state == 0)
			sysmon_hmx_stats->off_time =
			stats->sysmon_smem_hmx_power_stats.off_time / (1000*1000) + diff_time;

		sysmon_hmx_stats->current_clk = stats->sysmon_smem_hmx_power_stats.current_clk;
		sysmon_hmx_stats->num_hmxclk_levels =
		stats->sysmon_smem_hmx_power_stats.num_hmxclk_levels;
		sysmon_hmx_stats->current_hmx_state =
		stats->sysmon_smem_hmx_power_stats.current_hmx_state;
	} else {
		return -ENOKEY;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_cdsp_hmx_residency);

/**
 * sysmon_stats_query_power_residency() - * API to query requested
 * DSP subsystem power residency.On success, returns power residency
 * statistics in the given sysmon_smem_power_stats structure.
 */
int sysmon_stats_query_power_residency(enum dsp_id_t dsp_id,
					struct sysmon_smem_power_stats *sysmon_power_stats)
{
	int ret = 0, size = 0;
	u64 lpi_accumulated = 0;
	u64 lpm_accumulated = 0;
	struct sysmon_smem_power_stats_extended *ptr = NULL;
	struct sysmon_smem_power_stats smem_sysmon_power_stats_l = { 0 };

	if (!sysmon_power_stats) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_adsp)) {
			ptr = g_sysmon_stats.sysmon_power_stats_adsp;
			size = (((ptr->powerstats.version) >> 16) & 0xFFF) + sizeof(u32);
			if (copy_powerstats(&smem_sysmon_power_stats_l, dsp_id))
				return -ENOKEY;

			if (g_sysmon_stats.sleep_stats_adsp) {
				lpm_accumulated = g_sysmon_stats.sleep_stats_adsp->accumulated;
				if (g_sysmon_stats.sleep_stats_adsp->last_entered_at >
						g_sysmon_stats.sleep_stats_adsp->last_exited_at)
					lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_adsp->last_entered_at;
			}

			if (g_sysmon_stats.sleep_lpi_adsp) {
				lpi_accumulated = g_sysmon_stats.sleep_lpi_adsp->accumulated;
				if (g_sysmon_stats.sleep_lpi_adsp->last_entered_at >
					g_sysmon_stats.sleep_lpi_adsp->last_exited_at)
					lpi_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_lpi_adsp->last_entered_at;
			}
		} else
			ret = -ENOKEY;
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_cdsp)) {
			ptr = g_sysmon_stats.sysmon_power_stats_cdsp;
			size = (((ptr->powerstats.version) >> 16) & 0xFFF) + sizeof(u32);
			if (copy_powerstats(&smem_sysmon_power_stats_l, dsp_id))
				return -ENOKEY;

			if (g_sysmon_stats.sleep_stats_cdsp) {
				lpm_accumulated = g_sysmon_stats.sleep_stats_cdsp->accumulated;
				if (g_sysmon_stats.sleep_stats_cdsp->last_entered_at >
						g_sysmon_stats.sleep_stats_cdsp->last_exited_at)
					lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_cdsp->last_entered_at;
			}
		} else
			ret = -ENOKEY;
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_slpi)) {
			ptr = g_sysmon_stats.sysmon_power_stats_slpi;
			size = (((ptr->powerstats.version) >> 16) & 0xFFF) + sizeof(u32);
			if (copy_powerstats(&smem_sysmon_power_stats_l, dsp_id))
				return -ENOKEY;
			if (g_sysmon_stats.sleep_stats_slpi) {
				lpm_accumulated = g_sysmon_stats.sleep_stats_slpi->accumulated;
				if (g_sysmon_stats.sleep_stats_slpi->last_entered_at >
					g_sysmon_stats.sleep_stats_slpi->last_exited_at)
					lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_slpi->last_entered_at;
			}
			if (g_sysmon_stats.sleep_lpi_slpi) {
				lpi_accumulated = g_sysmon_stats.sleep_lpi_slpi->accumulated;
				if (g_sysmon_stats.sleep_lpi_slpi->last_entered_at >
					g_sysmon_stats.sleep_lpi_slpi->last_exited_at)
					lpi_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_lpi_slpi->last_entered_at;
			}
		} else
			ret = -ENOKEY;
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	if (ret == 0) {
		ret = add_delta_time((ptr->powerstats.version) & 0xFF, lpi_accumulated,
				lpm_accumulated, &smem_sysmon_power_stats_l, dsp_id);
		if (ret == 0) {
			memcpy(sysmon_power_stats, &smem_sysmon_power_stats_l,
				sizeof(struct sysmon_smem_power_stats));
			sysmon_power_stats->version = (ptr->powerstats.version) & 0xFF;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_power_residency);
/**
 * API to query requested DSP subsystem's Q6 clock and bandwidth.
 * On success, returns Q6 clock and bandwidth statistics in the given
 * sysmon_smem_q6_event_stats structure.
 */
int sysmon_stats_query_q6_votes(enum dsp_id_t dsp_id,
		struct sysmon_smem_q6_event_stats *sysmon_q6_event_stats)
{
	int ret = 0;
	int ver = 0;
	struct sysmon_smem_q6_event_stats_extended *events_ptr = NULL;

	if (!sysmon_q6_event_stats) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_adsp)) {
			events_ptr = g_sysmon_stats.sysmon_event_stats_adsp;
			sysmon_q6_event_stats->QDSP6_clk =
				events_ptr->event_stats.QDSP6_clk;
			sysmon_q6_event_stats->Ab_vote_lsb =
				events_ptr->event_stats.Ab_vote_lsb;
			sysmon_q6_event_stats->Ab_vote_msb =
				events_ptr->event_stats.Ab_vote_msb;
			sysmon_q6_event_stats->Ib_vote_lsb =
				events_ptr->event_stats.Ib_vote_lsb;
			sysmon_q6_event_stats->Ib_vote_msb =
				events_ptr->event_stats.Ib_vote_msb;
			sysmon_q6_event_stats->Sleep_latency =
				events_ptr->event_stats.Sleep_latency;
			sysmon_q6_event_stats->HMX_Power_state = -1;
			sysmon_q6_event_stats->HMX_clk = -1;
		} else {
			ret = -ENOKEY;
		}
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_cdsp)) {
			events_ptr = g_sysmon_stats.sysmon_event_stats_cdsp;
			ver = events_ptr->featureId_q6Event & 0xFFFF;
			sysmon_q6_event_stats->QDSP6_clk =
				events_ptr->event_stats.QDSP6_clk;
			sysmon_q6_event_stats->Ab_vote_lsb =
				events_ptr->event_stats.Ab_vote_lsb;
			sysmon_q6_event_stats->Ab_vote_msb =
				events_ptr->event_stats.Ab_vote_msb;
			sysmon_q6_event_stats->Ib_vote_lsb =
				events_ptr->event_stats.Ib_vote_lsb;
			sysmon_q6_event_stats->Ib_vote_msb =
				events_ptr->event_stats.Ib_vote_msb;
			sysmon_q6_event_stats->Sleep_latency =
				events_ptr->event_stats.Sleep_latency;
			if (ver > 1) {
				sysmon_q6_event_stats->HMX_Power_state =
					events_ptr->event_stats_ext.HMX_Power_state;
				sysmon_q6_event_stats->HMX_clk =
					events_ptr->event_stats_ext.HMX_clk;
			} else {
				sysmon_q6_event_stats->HMX_Power_state = -1;
				sysmon_q6_event_stats->HMX_clk = -1;
			}
		} else {
			ret = -ENOKEY;
		}
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_slpi)) {
			events_ptr = g_sysmon_stats.sysmon_event_stats_slpi;
			sysmon_q6_event_stats->QDSP6_clk =
				events_ptr->event_stats.QDSP6_clk;
			sysmon_q6_event_stats->Ab_vote_lsb =
				events_ptr->event_stats.Ab_vote_lsb;
			sysmon_q6_event_stats->Ab_vote_msb =
				events_ptr->event_stats.Ab_vote_msb;
			sysmon_q6_event_stats->Ib_vote_lsb =
				events_ptr->event_stats.Ib_vote_lsb;
			sysmon_q6_event_stats->Ib_vote_msb =
				events_ptr->event_stats.Ib_vote_msb;
			sysmon_q6_event_stats->Sleep_latency =
				events_ptr->event_stats.Sleep_latency;
			sysmon_q6_event_stats->HMX_Power_state = -1;
			sysmon_q6_event_stats->HMX_clk = -1;
		} else {
			ret = -ENOKEY;
		}
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_q6_votes);

/*Checks for DSP power collapse and resets the q6 average
 *load to zero if dsp is power collapsed.
 */
static u32 sysmon_read_q6_load(enum dsp_id_t dsp_id)
{
	struct sysmon_smem_q6_load_stats sysmon_q6_load;
	u64 last_q6load_update_at = 0;
	u64 curr_timestamp = __arch_counter_get_cntvct();

	switch (dsp_id) {
	case ADSP:
		memcpy(&sysmon_q6_load,
			g_sysmon_stats.q6_avg_load_adsp,
			sizeof(struct sysmon_smem_q6_load_stats));

		last_q6load_update_at =
			(u64) (((u64)sysmon_q6_load.Last_update_time_load_msb<<32)|
					sysmon_q6_load.Last_update_time_load_lsb);

		if ((curr_timestamp > last_q6load_update_at) &&
			((curr_timestamp - last_q6load_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
			sysmon_q6_load.q6load_avg = 0;
		}
	break;
	case CDSP:
		memcpy(&sysmon_q6_load,
			g_sysmon_stats.q6_avg_load_cdsp,
			sizeof(struct sysmon_smem_q6_load_stats));

		last_q6load_update_at =
			(u64) (((u64)sysmon_q6_load.Last_update_time_load_msb << 32)|
					sysmon_q6_load.Last_update_time_load_lsb);

		if ((curr_timestamp > last_q6load_update_at) &&
			((curr_timestamp - last_q6load_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
			sysmon_q6_load.q6load_avg = 0;
		}
	break;
	case SLPI:
		memcpy(&sysmon_q6_load,
			g_sysmon_stats.q6_avg_load_slpi,
			sizeof(struct sysmon_smem_q6_load_stats));

		last_q6load_update_at =
			(u64) (((u64)sysmon_q6_load.Last_update_time_load_msb << 32)|
						sysmon_q6_load.Last_update_time_load_lsb);

		if ((curr_timestamp > last_q6load_update_at) &&
			((curr_timestamp - last_q6load_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
			sysmon_q6_load.q6load_avg = 0;
		}
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		return -EINVAL;
	break;
	}

	return sysmon_q6_load.q6load_avg;
}

/**
 * API to query requested DSP subsystem's Q6 load.
 * On success, returns average Q6 load in KCPS for the given
 * q6load_avg parameter.
 */
int sysmon_stats_query_q6_load(enum dsp_id_t dsp_id, u32 *q6load_avg)
{
	int ret = 0;
	u32 q6_average_load = 0;

	if (!q6load_avg) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_adsp)) {
			q6_average_load = sysmon_read_q6_load(ADSP);
			memcpy(q6load_avg, &q6_average_load, sizeof(u32));
		} else
			ret = -ENOKEY;
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_cdsp)) {
			q6_average_load = sysmon_read_q6_load(CDSP);
			memcpy(q6load_avg, &q6_average_load, sizeof(u32));
		} else
			ret = -ENOKEY;
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_slpi)) {
			q6_average_load = sysmon_read_q6_load(SLPI);
			memcpy(q6load_avg, &q6_average_load, sizeof(u32));
		} else
			ret = -ENOKEY;
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	return ret;

}
EXPORT_SYMBOL_GPL(sysmon_stats_query_q6_load);

/*
 * Helper function to adjust accumulated sleep time if subsystem is currently in sleep
 */
static u64 adjust_sleep_accumulated_time(u64 *accumulated, u64 last_entered_at, u64 last_exited_at)
{
	u64 accumulated_local = *accumulated;

	if (last_entered_at > last_exited_at) {
		accumulated_local = *accumulated;
		accumulated_local += arch_timer_read_counter() - last_entered_at;
	}

	return accumulated_local;
}

/*
 * Helper function to copy LPM sleep stats with accumulated time adjustment
 */
static int copy_lpm_sleep_stats(struct sleep_stats *src, struct sleep_stats *dest)
{
	u64 accumulated = 0;

	if (IS_ERR_OR_NULL(src))
		return -ENOKEY;

	memcpy(dest, src, sizeof(struct sleep_stats));
	accumulated = adjust_sleep_accumulated_time(&src->accumulated,
					src->last_entered_at,
					src->last_exited_at);
	dest->accumulated = accumulated;
	return 0;
}

/*
 * Helper function to copy LPI sleep stats with accumulated time adjustment
 */
static int copy_lpi_sleep_stats(struct sleep_stats_island *src, struct sleep_stats_island *dest)
{
	u64 accumulated = 0;

	if (IS_ERR_OR_NULL(src))
		return -ENOKEY;

	memcpy(dest, src, sizeof(struct sleep_stats_island));
	accumulated = adjust_sleep_accumulated_time(&src->accumulated,
					src->last_entered_at,
					src->last_exited_at);
	dest->accumulated = accumulated;
	return 0;
}

/**
 * Generic function to process sleep stats for any DSP
 */
static int process_dsp_sleep_stats(const struct dsp_sleep_config *config,
				    struct sleep_stats *sleep_stats_lpm,
				    struct sleep_stats_island *sleep_stats_lpi)
{
	int ret = 0;

	if (!config) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	if (!(*config->smem_init_flag))
		config->init_func();

	if (sleep_stats_lpm)
		ret = copy_lpm_sleep_stats(config->lpm_stats, sleep_stats_lpm);

	if (sleep_stats_lpi && ret == 0)
		ret = copy_lpi_sleep_stats(config->lpi_stats, sleep_stats_lpi);

	return ret;
}

/**
 * API to query requested DSP subsystem sleep stats for
 * LPM and LPI.On success, returns sleep
 * statistics in the given sleep_stats structure for LPM and
 * LPI(on supported subsystems).
 */
int sysmon_stats_query_sleep(enum dsp_id_t dsp_id,
					struct sleep_stats *sleep_stats_lpm,
					struct sleep_stats_island *sleep_stats_lpi)
{
	struct dsp_sleep_config config;

	/* Validate input parameters based on DSP type */
	if (dsp_id == ADSP || dsp_id == SLPI) {
		if (!sleep_stats_lpm && !sleep_stats_lpi) {
			pr_err("%s: Null pointer received\n", __func__);
			return -EINVAL;
		}
	} else if (dsp_id == CDSP) {
		if (!sleep_stats_lpm) {
			pr_err("%s: Null pointer received\n", __func__);
			return -EINVAL;
		}
	} else {
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		return -EINVAL;
	}

	/* Configure DSP-specific parameters */
	switch (dsp_id) {
	case ADSP:
		config.smem_init_flag = &g_sysmon_stats.smem_init_adsp;
		config.init_func = sysmon_smem_init_adsp;
		config.lpm_stats = g_sysmon_stats.sleep_stats_adsp;
		config.lpi_stats = g_sysmon_stats.sleep_lpi_adsp;
		break;
	case CDSP:
		config.smem_init_flag = &g_sysmon_stats.smem_init_cdsp;
		config.init_func = sysmon_smem_init_cdsp;
		config.lpm_stats = g_sysmon_stats.sleep_stats_cdsp;
		config.lpi_stats = NULL;
		break;
	case SLPI:
		config.smem_init_flag = &g_sysmon_stats.smem_init_slpi;
		config.init_func = sysmon_smem_init_slpi;
		config.lpm_stats = g_sysmon_stats.sleep_stats_slpi;
		config.lpi_stats = g_sysmon_stats.sleep_lpi_slpi;
		break;
	default:
		return -EINVAL;
	}

	/* Process sleep stats using the generic function */
	return process_dsp_sleep_stats(&config, sleep_stats_lpm, sleep_stats_lpi);
}
EXPORT_SYMBOL_GPL(sysmon_stats_query_sleep);

static int master_adsp_stats_show(struct seq_file *s, void *d)
{
	int i = 0, j = 0;
	u64 lpi_accumulated = 0;
	u64 lpm_accumulated = 0;
	u32 q6_load;
	u8 ver = 0;
	struct sysmon_smem_power_stats sysmon_power_stats = { 0 };
	struct sysmon_smem_q6_event_stats_extended *events_ptr = NULL;

	if (!g_sysmon_stats.smem_init_adsp)
		sysmon_smem_init_adsp();

	if (g_sysmon_stats.sysmon_event_stats_adsp) {
		events_ptr = g_sysmon_stats.sysmon_event_stats_adsp;
		seq_puts(s, "\nsysMon stats:\n\n");
		seq_printf(s, "Core clock(KHz): %d\n",
				events_ptr->event_stats.QDSP6_clk);
		seq_printf(s, "Ab vote(Bytes): %llu\n",
				(((u64)events_ptr->event_stats.Ab_vote_msb << 32) |
					   events_ptr->event_stats.Ab_vote_lsb));
		seq_printf(s, "Ib vote(Bytes): %llu\n",
				(((u64)events_ptr->event_stats.Ib_vote_msb << 32) |
					   events_ptr->event_stats.Ib_vote_lsb));
		seq_printf(s, "Sleep latency(usec): %u\n",
				events_ptr->event_stats.Sleep_latency > 0 ?
				events_ptr->event_stats.Sleep_latency : U32_MAX);
	}

	if (g_sysmon_stats.dsppm_stats_adsp) {
		seq_puts(s, "\nDSPPM stats:\n\n");
		seq_printf(s, "Version: %u\n", g_sysmon_stats.dsppm_stats_adsp->version);
		seq_printf(s, "Sleep latency(usec): %u\n",
					g_sysmon_stats.dsppm_stats_adsp->latency_us ?
					g_sysmon_stats.dsppm_stats_adsp->latency_us : U32_MAX);
		seq_printf(s, "Timestamp: %u\n", g_sysmon_stats.dsppm_stats_adsp->timestamp);

		for (; i < DSPPMSTATS_NUMPD; i++) {
			seq_printf(s, "Pid: %d, Num active clients: %d\n",
						g_sysmon_stats.dsppm_stats_adsp->pd[i].pid,
						g_sysmon_stats.dsppm_stats_adsp->pd[i].num_active);
		}
	}

	if (g_sysmon_stats.sleep_stats_adsp) {
		lpm_accumulated = g_sysmon_stats.sleep_stats_adsp->accumulated;

		if (g_sysmon_stats.sleep_stats_adsp->last_entered_at >
					g_sysmon_stats.sleep_stats_adsp->last_exited_at)
			lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_adsp->last_entered_at;

		seq_puts(s, "\nLPM stats:\n\n");
		seq_printf(s, "Count = %u\n", g_sysmon_stats.sleep_stats_adsp->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_sysmon_stats.sleep_stats_adsp->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_sysmon_stats.sleep_stats_adsp->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n", lpm_accumulated);
	}

	if (g_sysmon_stats.sleep_lpi_adsp) {
		lpi_accumulated = g_sysmon_stats.sleep_lpi_adsp->accumulated;

		if (g_sysmon_stats.sleep_lpi_adsp->last_entered_at >
					g_sysmon_stats.sleep_lpi_adsp->last_exited_at)
			lpi_accumulated += arch_timer_read_counter() -
					g_sysmon_stats.sleep_lpi_adsp->last_entered_at;

		seq_puts(s, "\nLPI stats:\n\n");
		seq_printf(s, "Count = %u\n", g_sysmon_stats.sleep_lpi_adsp->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_sysmon_stats.sleep_lpi_adsp->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_sysmon_stats.sleep_lpi_adsp->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n",
			lpi_accumulated);
	}

	if (g_sysmon_stats.sysmon_power_stats_adsp) {

		if (sysmon_stats_query_power_residency(ADSP, &sysmon_power_stats))
			return -ENOKEY;

		seq_puts(s, "\nPower Stats:\n\n");
		for (j = 0; j < SYSMON_POWER_STATS_MAX_CLK_LEVELS; j++) {
			if (sysmon_power_stats.clk_arr[j]) {
				if (ver >= 2) {
					seq_printf(s, "%u : Core Clock(KHz) : %u \tActive Time(sec) : %u \tLPI time(sec) : %u\n",
						j,
						sysmon_power_stats.clk_arr[j],
						sysmon_power_stats.active_time[j],
						sysmon_power_stats.island_time[j]);
				} else {
					seq_printf(s, "%u : Core Clock(KHz): %u \tActive Time(sec): %u\n",
						j,
						sysmon_power_stats.clk_arr[j],
						sysmon_power_stats.active_time[j]);
				}
			}
		}

		seq_printf(s, "Power collapse time(sec) = %u\n",
			sysmon_power_stats.pc_time);
		seq_printf(s, "Total LPI time(sec) = %u\n",
			sysmon_power_stats.lpi_time);
		if (ver >= 2)
			seq_printf(s, "Current core clock(KHz) = %u\n",
				sysmon_power_stats.current_clk);
	}

	if (g_sysmon_stats.q6_avg_load_adsp) {
		seq_puts(s, "\nQ6 load:\n\n");
		q6_load = sysmon_read_q6_load(ADSP);
		seq_printf(s, "Average Q6 load in KCPS = %u\n", q6_load);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(master_adsp_stats);

static int master_cdsp_stats_show(struct seq_file *s, void *d)
{
	int i = 0, j = 0, ret = 0;
	u32 hmx_util, hvx_util;
	u32 q6_load;
	u64 lpm_accumulated = 0;
	u8 ver = 0;
	struct sysmon_smem_power_stats sysmon_power_stats = { 0 };
	struct sysmon_smem_ddr_stats sysmon_ddr_stats = {0};
	struct sysmon_smem_q6_event_stats_extended *events_ptr = NULL;
	struct smem_hmx_power_stats sysmon_hmx_power_stats = { 0 };
	struct smem_bw_stats_ioctl bw_stats = { 0 };

	if (!g_sysmon_stats.smem_init_cdsp)
		sysmon_smem_init_cdsp();

	if (g_sysmon_stats.sysmon_event_stats_cdsp) {
		events_ptr = g_sysmon_stats.sysmon_event_stats_cdsp;
		ver = events_ptr->featureId_q6Event & 0xFFFF;
		seq_puts(s, "\nsysMon stats:\n\n");
		seq_printf(s, "Core clock(KHz): %d\n",
				events_ptr->event_stats.QDSP6_clk);
		seq_printf(s, "Ab vote(Bytes): %llu\n",
			(((u64)events_ptr->event_stats.Ab_vote_msb << 32) |
					   events_ptr->event_stats.Ab_vote_lsb));
		seq_printf(s, "Ib vote(Bytes): %llu\n",
			(((u64)events_ptr->event_stats.Ib_vote_msb << 32) |
					   events_ptr->event_stats.Ib_vote_lsb));
		seq_printf(s, "Sleep latency(usec): %u\n",
			events_ptr->event_stats.Sleep_latency > 0 ?
			events_ptr->event_stats.Sleep_latency : U32_MAX);
		if ((ver > 1) &&
			(events_ptr->event_stats_ext.HMX_Power_state >= 0)) {
			seq_printf(s, "HMX Power state: %s\n",
				events_ptr->event_stats_ext.HMX_Power_state ? "ON" : "OFF");
			seq_printf(s, "HMX Clock(KHz): %d\n",
				events_ptr->event_stats_ext.HMX_clk);
		}
	}

	if (g_sysmon_stats.dsppm_stats_cdsp) {
		seq_puts(s, "\nDSPPM stats:\n\n");
		seq_printf(s, "Version: %u\n",
			g_sysmon_stats.dsppm_stats_cdsp->version);
		seq_printf(s, "Sleep latency(usec): %u\n",
			g_sysmon_stats.dsppm_stats_cdsp->latency_us ?
			g_sysmon_stats.dsppm_stats_cdsp->latency_us : U32_MAX);
		seq_printf(s, "Timestamp: %u\n",
			g_sysmon_stats.dsppm_stats_cdsp->timestamp);

		for (; i < DSPPMSTATS_NUMPD; i++) {
			seq_printf(s, "Pid: %d, Num active clients: %d\n",
						g_sysmon_stats.dsppm_stats_cdsp->pd[i].pid,
						g_sysmon_stats.dsppm_stats_cdsp->pd[i].num_active);
		}
	}

	if (g_sysmon_stats.sleep_stats_cdsp) {
		lpm_accumulated = g_sysmon_stats.sleep_stats_cdsp->accumulated;

		if (g_sysmon_stats.sleep_stats_cdsp->last_entered_at >
					g_sysmon_stats.sleep_stats_cdsp->last_exited_at)
			lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_cdsp->last_entered_at;

		seq_puts(s, "\nLPM stats:\n\n");
		seq_printf(s, "Count = %u\n",
			g_sysmon_stats.sleep_stats_cdsp->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_sysmon_stats.sleep_stats_cdsp->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_sysmon_stats.sleep_stats_cdsp->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n", lpm_accumulated);
	}

	if (g_sysmon_stats.sysmon_power_stats_cdsp) {

		if (!sysmon_stats_query_power_residency(CDSP, &sysmon_power_stats)) {

			seq_puts(s, "\nPower Stats:\n\n");

			for (j = 0; j < SYSMON_POWER_STATS_MAX_CLK_LEVELS; j++) {
				if (sysmon_power_stats.clk_arr[j])
					seq_printf(s, "%u : Core Clock(KHz) : %u \tActive Time(sec) : %u\n",
						j,
						sysmon_power_stats.clk_arr[j],
						sysmon_power_stats.active_time[j]);
			}

			seq_printf(s, "Power collapse time(sec) = %u\n",
				sysmon_power_stats.pc_time);
			seq_printf(s, "Total LPI time(sec) = %u\n",
				sysmon_power_stats.lpi_time);

			if (ver >= 2)
				seq_printf(s, "Current core clock(KHz) = %u\n",
					sysmon_power_stats.current_clk);
		} else {
			seq_puts(s, "\n Failed to query CDSP Power Stats\n\n");
		}
	}

	if (g_sysmon_stats.sysmon_ddr_stats) {

		seq_puts(s, "\nDDR stats :\n");

		if (!sysmon_stats_query_ddr_residency(&sysmon_ddr_stats)) {
			for (j = 0; j < sysmon_ddr_stats.num_ddrclk_levels; j++) {
				seq_printf(s, "%u : DDR Clock(KHz) : %u \tActive Time(sec) : %u\n",
					j,
					sysmon_ddr_stats.clk_arr[j],
					sysmon_ddr_stats.active_time[j]);
			}

		seq_printf(s, "Number of DDR clock levels = %u\n",
			sysmon_ddr_stats.num_ddrclk_levels);

		seq_printf(s, "Power collapse time(sec) = %u\n",
			sysmon_ddr_stats.pc_time);

		seq_printf(s, "Current DDR clock(KHz) = %u\n",
				sysmon_ddr_stats.curr_clk);
		} else {
			seq_puts(s, "DDR Stats Query API failed\n");
		}
	}

	if (g_sysmon_stats.sysmon_hmx_power_stats) {
		seq_puts(s, "\nCDSP HMX residency stats :\n");

		if (!sysmon_stats_query_cdsp_hmx_residency(&sysmon_hmx_power_stats)) {
			for (j = 0; j < sysmon_hmx_power_stats.num_hmxclk_levels; j++) {
				seq_printf(s, "%u : HMX Clock(KHz) : %u \tON Time(sec) : %llu\n",
					j,
					sysmon_hmx_power_stats.clk_arr[j],
					sysmon_hmx_power_stats.on_time[j]);
			}
			seq_printf(s, "Number of HMX clock levels = %u\n",
				sysmon_hmx_power_stats.num_hmxclk_levels);
			seq_printf(s, "OFF time(sec) = %llu\n",
				sysmon_hmx_power_stats.off_time);
			seq_printf(s, "Current HMX clock state = %u\n",
				sysmon_hmx_power_stats.current_hmx_state);
			seq_printf(s, "Current HMX clock(KHz) = %u\n",
						sysmon_hmx_power_stats.current_clk);
		} else {
			seq_puts(s, "CDSP HMX residency stats Query API failed\n");
		}
	}

	if (g_sysmon_stats.sysmon_bw_stats) {
		if (!sysmon_stats_query_bw_stats(&bw_stats)) {
			seq_puts(s, "\nCDSP BW residency stats :\n");
			seq_printf(s, "BW stats feature structure Size : %d bytes\n",
						bw_stats.struct_size);
			seq_printf(s, "BW stats feature driver capability mask : %d\n",
						bw_stats.bw_stats_driver_capability_mask);
			seq_printf(s, "BandWidth sample window : %d ms\n",
						bw_stats.sampling_window_in_ms);
			seq_printf(s, "current BandWidth : %d kb/s\n", bw_stats.bw_sample_in_kbps);
			seq_printf(s, "Total accumulated BandWidth : %llu bytes\n",
						bw_stats.accum_bw_mon_val);
			seq_printf(s, "BandWidth stats last updated : 0x%llx\n",
						bw_stats.last_updated_timestamp);
			seq_printf(s, "Current timestamp : 0x%llx\n", bw_stats.current_timestamp);
		} else {
			seq_puts(s, "CDSP BW residency stats Query API failed\n");
		}
	}

	if (g_sysmon_stats.q6_avg_load_cdsp) {
		seq_puts(s, "\nQ6 load:\n\n");
		q6_load = sysmon_read_q6_load(CDSP);
		seq_printf(s, "Average Q6 load in KCPS = %u\n", q6_load);
	}

	ret = sysmon_stats_query_hmx_utlization(&hmx_util);

	if (ret) {
		seq_printf(s, "\nHMX stats not available, error code: %d\n", ret);
	} else {
		seq_puts(s, "\nHMX stats:\n\n");
		seq_printf(s, "HMX utilization in percentage = %u\n", hmx_util);
	}

	ret = sysmon_stats_query_hvx_utlization(&hvx_util);

	if (ret) {
		seq_printf(s, "\nHVX stats not available, error code: %d\n", ret);
	} else {
		seq_puts(s, "\nHVX Stats:\n\n");
		seq_printf(s, "HVX utilization in percentage = %u\n", hvx_util);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(master_cdsp_stats);

/**
 * Helper function to handle simple u32 utilization queries
 */
static int handle_utilization_query(unsigned long ioctl_params,
				     int (*query_func)(u32 *),
				     const char *func_name,
				     const char *util_type)
{
	u32 util;
	int ret;

	ret = query_func(&util);
	if (ret) {
		pr_err("%s failed with return value: %d\n", func_name, ret);
		return ret;
	}

	if (!access_ok((u32 __user *)ioctl_params, sizeof(u32))) {
		pr_err("User-space pointer is not accessible\n");
		return -EFAULT;
	}

	if (copy_to_user((u32 __user *)ioctl_params, &util, sizeof(u32))) {
		pr_err("copy_to_user failed in %s data read\n", util_type);
		return -EFAULT;
	}

	return 0;
}

/**
 * Helper function to validate user-space buffer and copy size
 */
static int validate_and_get_buffer_size(unsigned long ioctl_params,
					u32 *buffer_size,
					const char *feature_name)
{
	if (!access_ok((u32 __user *)ioctl_params, sizeof(u32))) {
		pr_err("User-space pointer for %s is not accessible\n", feature_name);
		return -EFAULT;
	}

	if (copy_from_user(buffer_size, (u32 __user *)ioctl_params, sizeof(u32))) {
		pr_err("copy_from_user failed for %s buffer size\n", feature_name);
		return -EFAULT;
	}

	if (!access_ok((u32 __user *)ioctl_params, *buffer_size)) {
		pr_err("User-space buffer for %s is not accessible\n", feature_name);
		return -EFAULT;
	}

	return 0;
}

/**
 * Helper function to validate buffer size against minimum requirement
 */
static int validate_buffer_size(u32 buffer_size, u32 min_size, const char *feature_name)
{
	if (buffer_size < min_size) {
		pr_err("User-space buffer size (%u) for %s is smaller than required (%u)\n",
			buffer_size, feature_name, min_size);
		return -EINVAL;
	}
	return 0;
}

/**
 * Helper function to copy data to user space
 */
static int copy_stats_to_user(unsigned long ioctl_params,
			      void *kernel_data,
			      size_t copy_size,
			      const char *feature_name)
{
	if (copy_to_user((void __user *)ioctl_params, kernel_data, copy_size)) {
		pr_err("copy_to_user failed for %s\n", feature_name);
		return -EFAULT;
	}
	return 0;
}

/**
 * Helper function to handle BW stats IOCTL
 */
static int handle_bw_stats_ioctl(unsigned long ioctl_params)
{
	struct smem_bw_stats_ioctl sysmon_bw_stats;
	u32 bw_stats_size;
	int ret;

	ret = sysmon_stats_query_bw_stats(&sysmon_bw_stats);
	if (ret) {
		pr_err("sysmon_stats_query_bw_stats failed with return value: %d\n", ret);
		return ret;
	}

	ret = validate_and_get_buffer_size(ioctl_params, &bw_stats_size, "BW stats");
	if (ret)
		return ret;

	ret = validate_buffer_size(bw_stats_size, BWMON_STATS_INITIAL_FEATURE_SIZE, "BW stats");
	if (ret)
		return ret;

	if (bw_stats_size > sizeof(struct smem_bw_stats_ioctl))
		bw_stats_size = sizeof(struct smem_bw_stats_ioctl);

	sysmon_bw_stats.struct_size = bw_stats_size;

	return copy_stats_to_user(ioctl_params, &sysmon_bw_stats, bw_stats_size, "BW stats");
}

/**
 * Helper function to handle sleep stats IOCTL
 */
static int handle_sleep_stats_ioctl(unsigned long ioctl_params)
{
	struct sleep_stats_ioctl_ext sleep_stats_lpm = {};
	struct sleep_stats sleep_stats_local = {};
	u32 sleep_stats_size;
	int ret;

	ret = sysmon_stats_query_sleep(CDSP, &sleep_stats_local, NULL);
	if (ret) {
		pr_err("sysmon_stats_query_sleep failed with return value: %d\n", ret);
		return ret;
	}

	ret = validate_and_get_buffer_size(ioctl_params, &sleep_stats_size, "CDSP sleep stats");
	if (ret)
		return ret;

	ret = validate_buffer_size(sleep_stats_size, SLEEP_STATS_INITIAL_FEATURE_SIZE,
				   "CDSP sleep stats");
	if (ret)
		return ret;

	sleep_stats_lpm.sleep_stats_driver_capability_mask = SLEEP_STATS_DEFAULT_FEATURE_MASK;
	sleep_stats_lpm.sleep_stats.stat_type = sleep_stats_local.stat_type;
	sleep_stats_lpm.sleep_stats.count = sleep_stats_local.count;
	sleep_stats_lpm.sleep_stats.last_entered_at = sleep_stats_local.last_entered_at;
	sleep_stats_lpm.sleep_stats.last_exited_at = sleep_stats_local.last_exited_at;
	sleep_stats_lpm.sleep_stats.accumulated = sleep_stats_local.accumulated;

	if (sleep_stats_size > sizeof(sleep_stats_lpm))
		sleep_stats_size = sizeof(sleep_stats_lpm);

	sleep_stats_lpm.struct_size = sleep_stats_size;

	return copy_stats_to_user(ioctl_params, &sleep_stats_lpm, sleep_stats_size,
				  "CDSP sleep stats");
}

/**
 * Helper function to handle Q6 votes IOCTL
 */
static int handle_q6_votes_ioctl(unsigned long ioctl_params)
{
	struct sysmon_smem_q6_event_stats_ioctl_ext q6_event_stats_cdsp = {};
	struct sysmon_smem_q6_event_stats  q6_event_stats_cdsp_local = {};
	u32 q6_votes_size;
	int ret;

	ret = sysmon_stats_query_q6_votes(CDSP, &q6_event_stats_cdsp_local);
	if (ret) {
		pr_err("sysmon_stats_query_q6_votes failed with return value: %d\n", ret);
		return ret;
	}

	ret = validate_and_get_buffer_size(ioctl_params, &q6_votes_size, "CDSP Q6 votes");
	if (ret)
		return ret;

	ret = validate_buffer_size(q6_votes_size, Q6_EVENT_STATS_INITIAL_FEATURE_SIZE,
				   "CDSP Q6 votes");
	if (ret)
		return ret;

	q6_event_stats_cdsp.q6_event_stats_driver_capability_mask =
			Q6_EVENT_STATS_DEFAULT_FEATURE_MASK;
	q6_event_stats_cdsp.q6_event_stats.QDSP6_clk =
			q6_event_stats_cdsp_local.QDSP6_clk;
	q6_event_stats_cdsp.q6_event_stats.Ab_vote_lsb =
			q6_event_stats_cdsp_local.Ab_vote_lsb;
	q6_event_stats_cdsp.q6_event_stats.Ab_vote_msb =
			q6_event_stats_cdsp_local.Ab_vote_msb;
	q6_event_stats_cdsp.q6_event_stats.Ib_vote_lsb =
			q6_event_stats_cdsp_local.Ib_vote_lsb;
	q6_event_stats_cdsp.q6_event_stats.Ib_vote_msb =
			q6_event_stats_cdsp_local.Ib_vote_msb;
	q6_event_stats_cdsp.q6_event_stats.Sleep_latency =
			q6_event_stats_cdsp_local.Sleep_latency;
	q6_event_stats_cdsp.q6_event_stats.HMX_Power_state =
			q6_event_stats_cdsp_local.HMX_Power_state;
	q6_event_stats_cdsp.q6_event_stats.HMX_clk =
			q6_event_stats_cdsp_local.HMX_clk;

	if (q6_votes_size > sizeof(q6_event_stats_cdsp))
		q6_votes_size = sizeof(q6_event_stats_cdsp);

	q6_event_stats_cdsp.struct_size = q6_votes_size;

	return copy_stats_to_user(ioctl_params, &q6_event_stats_cdsp, q6_votes_size,
				  "CDSP Q6 votes");
}

/**
 * Helper function to handle power residency IOCTL
 */
static int handle_power_residency_ioctl(unsigned long ioctl_params)
{
	struct sysmon_smem_power_stats_ioctl_ext power_stats_cdsp = {};
	struct sysmon_smem_power_stats power_stats_cdsp_local = {};
	u32 power_residency_size;
	int ret, i = 0;

	ret = sysmon_stats_query_power_residency(CDSP, &power_stats_cdsp_local);
	if (ret) {
		pr_err("sysmon_stats_query_power_residency failed with return value: %d\n", ret);
		return ret;
	}

	ret = validate_and_get_buffer_size(ioctl_params, &power_residency_size,
					   "CDSP power residency");
	if (ret)
		return ret;

	ret = validate_buffer_size(power_residency_size, POWER_STATS_INITIAL_FEATURE_SIZE,
				   "CDSP power residency");
	if (ret)
		return ret;

	power_stats_cdsp.power_stats_driver_capability_mask = POWER_STATS_DEFAULT_FEATURE_MASK;
	power_stats_cdsp.power_stats.version = power_stats_cdsp_local.version;

	for (i = 0; i < SYSMON_POWER_STATS_MAX_CLK_LEVELS; i++) {
		power_stats_cdsp.power_stats.clk_arr[i] =
			power_stats_cdsp_local.clk_arr[i];
		power_stats_cdsp.power_stats.active_time[i] =
			power_stats_cdsp_local.active_time[i];
	}

	power_stats_cdsp.power_stats.pc_time = power_stats_cdsp_local.pc_time;
	power_stats_cdsp.power_stats.lpi_time = power_stats_cdsp_local.lpi_time;
	power_stats_cdsp.power_stats.current_clk = power_stats_cdsp_local.current_clk;

	if (power_residency_size > sizeof(power_stats_cdsp))
		power_residency_size = sizeof(power_stats_cdsp);

	power_stats_cdsp.struct_size = power_residency_size;

	return copy_stats_to_user(ioctl_params, &power_stats_cdsp, power_residency_size,
				  "CDSP power residency");
}

/**
 * Helper function to handle CDSP utilization IOCTL
 */
static int handle_cdsp_utilization_ioctl(unsigned long ioctl_params)
{
	u32 q6_avg_load;
	int ret;

	ret = sysmon_stats_query_q6_load(CDSP, &q6_avg_load);
	if (ret) {
		pr_err("sysmon_stats_query_q6_load failed with return value: %d\n", ret);
		return ret;
	}

	if (!access_ok((u32 __user *)ioctl_params, sizeof(u32))) {
		pr_err("User-space pointer is not accessible\n");
		return -EFAULT;
	}

	if (copy_to_user((u32 __user *)ioctl_params, &q6_avg_load, sizeof(u32))) {
		pr_err("copy_to_user failed in Q6 load data read\n");
		return -EFAULT;
	}

	return 0;
}

static long dsp_utilization_stats_ioctl(struct file *file, unsigned int ioctl_num,
			unsigned long ioctl_params)
{
	u32 cmd = _IOC_NR(ioctl_num);

	switch (cmd) {
	case HVX_UTILIZATION:
		return handle_utilization_query(ioctl_params,
						sysmon_stats_query_hvx_utlization,
						"sysmon_stats_query_hvx_utlization",
						"HVX Utilization");
	case HMX_UTILIZATION:
		return handle_utilization_query(ioctl_params,
						sysmon_stats_query_hmx_utlization,
						"sysmon_stats_query_hmx_utlization",
						"HMX Utilization");
	case HLX_UTILIZATION:
		return handle_utilization_query(ioctl_params,
						sysmon_stats_query_hlx_utilization,
						"sysmon_stats_query_hlx_utilization",
						"HLX Utilization");
	case CDSP_UTILIZATION:
		return handle_cdsp_utilization_ioctl(ioctl_params);
	case CDSP_BW_MON_STATS:
		return handle_bw_stats_ioctl(ioctl_params);
	case CDSP_SLEEP_STATS:
		return handle_sleep_stats_ioctl(ioctl_params);
	case CDSP_Q6_VOTES:
		return handle_q6_votes_ioctl(ioctl_params);
	case CDSP_POWER_RESIDENCY:
		return handle_power_residency_ioctl(ioctl_params);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations sysmon_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dsp_utilization_stats_ioctl,
};

static int  __init sysmon_stats_init(void)
{

	u32 ret;
	struct subsystem_stats *me = &g_subsystem_stats;

	g_sysmon_stats.debugfs_dir = debugfs_create_dir("sysmon_subsystem_stats", NULL);

	if (!g_sysmon_stats.debugfs_dir) {
		pr_err("Failed to create debugfs directory for sysmon_subsystem_stats\n");
		goto debugfs_bail;
	}

	g_sysmon_stats.debugfs_master_adsp_stats =
			debugfs_create_file("master_adsp_stats",
			 0444, g_sysmon_stats.debugfs_dir, NULL, &master_adsp_stats_fops);

	if (!g_sysmon_stats.debugfs_master_adsp_stats)
		pr_err("Failed to create debugfs file for ADSP master stats\n");

	g_sysmon_stats.debugfs_master_cdsp_stats =
			debugfs_create_file("master_cdsp_stats",
			 0444, g_sysmon_stats.debugfs_dir, NULL, &master_cdsp_stats_fops);

	if (!g_sysmon_stats.debugfs_master_cdsp_stats)
		pr_err("Failed to create debugfs file for CDSP master stats\n");

	ret = alloc_chrdev_region(&me->devno, 0, 1, SUBSYSTEMSTATS_DEVICE_NAME_LOCAL);

	if (ret != 0) {
		pr_err("Cannot create sysmon_subsystem_stats char device\n");
		goto chardev_region_fail;
	}

	cdev_init(&me->sysmon_cdev, &sysmon_fops);
	me->sysmon_cdev.owner = THIS_MODULE;

	if ((cdev_add(&me->sysmon_cdev, MKDEV(MAJOR(me->devno), 0), 1)) != 0) {
		pr_err("Cannot add the subsystem stats device to the system\n");
		goto cdev_add_fail;
	}

	me->dev_class = class_create(SUBSYSTEMSTATS_CLASS_NAME_LOCAL);

	if (IS_ERR(me->dev_class)) {
		pr_err("Cannot create the subsystem stats class\n");
		goto class_bail;
	}

	if (IS_ERR(device_create(me->dev_class, NULL,
						MKDEV(MAJOR(me->devno), 0),
						NULL,
						SUBSYSTEMSTATS_DEVICE_NAME_LOCAL))) {
		pr_err("Cannot create subsystem stats Device\n");
		goto device_bail;
	}

	pr_debug("Subsystem stats device inserted\n");
	return 0;
device_bail:
	class_destroy(me->dev_class);
class_bail:
	cdev_del(&g_subsystem_stats.sysmon_cdev);
cdev_add_fail:
	unregister_chrdev_region(me->devno, 1);
chardev_region_fail:
	debugfs_remove_recursive(g_sysmon_stats.debugfs_dir);
debugfs_bail:
	return 0;
}

static void __exit sysmon_stats_exit(void)
{
	debugfs_remove_recursive(g_sysmon_stats.debugfs_dir);
	device_destroy(g_subsystem_stats.dev_class, g_subsystem_stats.sysmon_cdev.dev);
	class_destroy(g_subsystem_stats.dev_class);
	cdev_del(&g_subsystem_stats.sysmon_cdev);
	unregister_chrdev_region(g_subsystem_stats.devno, 1);
	pr_debug("Removed subsystem stats device\n");
}

module_init(sysmon_stats_init);
module_exit(sysmon_stats_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Sysmon subsystem Stats driver");
