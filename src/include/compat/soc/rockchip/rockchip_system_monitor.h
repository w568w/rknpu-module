/* SPDX-License-Identifier: GPL-2.0 */
/* Stub header for out-of-tree rknpu module build */

#ifndef __SOC_ROCKCHIP_SYSTEM_MONITOR_H
#define __SOC_ROCKCHIP_SYSTEM_MONITOR_H

struct monitor_dev_profile {
	/* stub */
};

struct monitor_dev_info {
	/* stub */
};

static inline struct monitor_dev_info *
rockchip_system_monitor_register(struct device *dev,
				 struct monitor_dev_profile *profile)
{
	return NULL;
}

static inline void
rockchip_system_monitor_unregister(struct monitor_dev_info *info)
{
}

#endif /* __SOC_ROCKCHIP_SYSTEM_MONITOR_H */
