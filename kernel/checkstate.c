// SPDX-License-Identifier: GPL-2.0-only
/*
 * /dev/checkstate: userspace access to the CDSP kernel APIs (see checkstate_ioctl.h):
 *   CHECKSTATE_IOC_SLEEP           -> sysmon_stats_query_sleep()
 *   CHECKSTATE_IOC_Q6_VOTES        -> sysmon_stats_query_q6_votes()
 *   CHECKSTATE_IOC_POWER_RESIDENCY -> sysmon_stats_query_power_residency()
 *   CHECKSTATE_IOC_DDR_RESIDENCY   -> sysmon_stats_query_ddr_residency()
 *   CHECKSTATE_IOC_DCVS_CLIENTS    -> cdsprm_sysmon_dcvs_clients_votes()
 * The sysmon driver exports these APIs to kernel modules only; its own ioctls do
 * not cover sleep and clock on every build.
 *
 * The DCVS clients query is an rpmsg round trip to CDSP and wakes CDSP up; the
 * others read SMEM. With dcvs_log=Y cdsprm also prints the DCVS votes to the
 * kernel log:
 *   echo Y > /sys/module/checkstate/parameters/dcvs_log
 */
#define pr_fmt(fmt) "checkstate: " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "sysmon_subsystem_stats.h"
#include "cdsprm_dcvs_clients_votes.h"
#include "checkstate_ioctl.h"

static bool dcvs_log;
module_param(dcvs_log, bool, 0644);
MODULE_PARM_DESC(dcvs_log, "also let cdsprm print DCVS clients votes to the kernel log (default N)");

static DEFINE_MUTEX(dcvs_lock);

/* Copy an API result to userspace, or return the API error */
static long copy_result(int ret, void __user *uarg, const void *st, size_t size)
{
	if (ret)
		return ret;
	return copy_to_user(uarg, st, size) ? -EFAULT : 0;
}

static long ioctl_dcvs_clients(void __user *uarg)
{
	struct sysmon_dcvs_clients *info;
	long ret;

	/* ~3.5KB, keep it off the stack */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_lock(&dcvs_lock);
	ret = cdsprm_sysmon_dcvs_clients_votes(info, READ_ONCE(dcvs_log));
	mutex_unlock(&dcvs_lock);

	/* Failures are positive DCVS_CLIENTS_VOTES_* codes, which ioctl() would report as success */
	if (ret) {
		pr_err("cdsprm_sysmon_dcvs_clients_votes failed: %ld\n", ret);
		ret = -EIO;
	} else {
		ret = copy_result(0, uarg, info, sizeof(*info));
	}

	kfree(info);
	return ret;
}

static long checkstate_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case CHECKSTATE_IOC_SLEEP: {
		struct sleep_stats st = {};

		/* CDSP has no LPI (island) stats */
		return copy_result(sysmon_stats_query_sleep(CDSP, &st, NULL), uarg, &st, sizeof(st));
	}
	case CHECKSTATE_IOC_Q6_VOTES: {
		struct sysmon_smem_q6_event_stats st = {};

		return copy_result(sysmon_stats_query_q6_votes(CDSP, &st), uarg, &st, sizeof(st));
	}
	case CHECKSTATE_IOC_POWER_RESIDENCY: {
		struct sysmon_smem_power_stats st = {};

		return copy_result(sysmon_stats_query_power_residency(CDSP, &st), uarg, &st, sizeof(st));
	}
	case CHECKSTATE_IOC_DDR_RESIDENCY: {
		struct sysmon_smem_ddr_stats st = {};

		return copy_result(sysmon_stats_query_ddr_residency(&st), uarg, &st, sizeof(st));
	}
	case CHECKSTATE_IOC_DCVS_CLIENTS:
		return ioctl_dcvs_clients(uarg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations checkstate_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = checkstate_ioctl,
};

static struct miscdevice checkstate_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "checkstate",
	.fops = &checkstate_fops,
	.mode = 0440,
};

module_misc_device(checkstate_dev);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Userspace access to CDSP sleep, clock and DCVS clients kernel APIs");
