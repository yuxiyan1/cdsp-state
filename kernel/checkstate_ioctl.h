#ifndef __CHECKSTATE_IOCTL_H__
#define __CHECKSTATE_IOCTL_H__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define CHECKSTATE_IOC_MAGIC	0xC5

#define CHECKSTATE_IOC_SLEEP \
	_IOR(CHECKSTATE_IOC_MAGIC, 1, struct sleep_stats)

#define CHECKSTATE_IOC_Q6_VOTES \
	_IOR(CHECKSTATE_IOC_MAGIC, 2, struct sysmon_smem_q6_event_stats)

#define CHECKSTATE_IOC_POWER_RESIDENCY \
	_IOR(CHECKSTATE_IOC_MAGIC, 3, struct sysmon_smem_power_stats)

#define CHECKSTATE_IOC_DDR_RESIDENCY \
	_IOR(CHECKSTATE_IOC_MAGIC, 4, struct sysmon_smem_ddr_stats)

#define CHECKSTATE_IOC_DCVS_CLIENTS \
	_IOR(CHECKSTATE_IOC_MAGIC, 5, struct sysmon_dcvs_clients)

#endif
