/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ioctls of /dev/checkstate (checkstate.ko), shared by checkstate.c and cdsp_state.cpp.
 * Each one calls the kernel API below for CDSP and copies its struct to the user buffer.
 * The struct types must be defined before using these macros.
 */
#ifndef __CHECKSTATE_IOCTL_H__
#define __CHECKSTATE_IOCTL_H__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define CHECKSTATE_IOC_MAGIC	0xC5

/* sysmon_stats_query_sleep(CDSP, ...): SMEM read */
#define CHECKSTATE_IOC_SLEEP \
	_IOR(CHECKSTATE_IOC_MAGIC, 1, struct sleep_stats)

/* sysmon_stats_query_q6_votes(CDSP, ...): SMEM read */
#define CHECKSTATE_IOC_Q6_VOTES \
	_IOR(CHECKSTATE_IOC_MAGIC, 2, struct sysmon_smem_q6_event_stats)

/* sysmon_stats_query_power_residency(CDSP, ...): SMEM read */
#define CHECKSTATE_IOC_POWER_RESIDENCY \
	_IOR(CHECKSTATE_IOC_MAGIC, 3, struct sysmon_smem_power_stats)

/* sysmon_stats_query_ddr_residency(): SMEM read */
#define CHECKSTATE_IOC_DDR_RESIDENCY \
	_IOR(CHECKSTATE_IOC_MAGIC, 4, struct sysmon_smem_ddr_stats)

/* cdsprm_sysmon_dcvs_clients_votes(): rpmsg round trip, wakes CDSP up */
#define CHECKSTATE_IOC_DCVS_CLIENTS \
	_IOR(CHECKSTATE_IOC_MAGIC, 5, struct sysmon_dcvs_clients)

#endif /* __CHECKSTATE_IOCTL_H__ */
