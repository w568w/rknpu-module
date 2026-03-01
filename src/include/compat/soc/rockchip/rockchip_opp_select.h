/* SPDX-License-Identifier: GPL-2.0 */
/* Stub header for out-of-tree rknpu module build */

#ifndef __SOC_ROCKCHIP_OPP_SELECT_H
#define __SOC_ROCKCHIP_OPP_SELECT_H

#include <linux/of.h>

struct rockchip_opp_info {
	int dummy;
};

static inline int rockchip_nvmem_cell_read_u8(struct device_node *np,
					      const char *cell_id, u8 *val)
{
	return -ENOSYS;
}

#endif /* __SOC_ROCKCHIP_OPP_SELECT_H */
