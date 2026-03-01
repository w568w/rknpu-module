// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/scatterlist.h>

#include "rknpu_iommu.h"

#define RKNPU_SWITCH_DOMAIN_WAIT_TIME_MS 6000

/*
 * Use standard kernel DMA mapping API instead of directly manipulating
 * internal iommu_dma_cookie / iova_domain structures, which are opaque
 * to out-of-tree modules and whose layout changes across kernel versions.
 */

int rknpu_iommu_dma_map_sg(struct device *dev, struct scatterlist *sg,
			   int nents, enum dma_data_direction dir,
			   bool iova_aligned)
{
	return dma_map_sg(dev, sg, nents, dir);
}

void rknpu_iommu_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
			      int nents, enum dma_data_direction dir,
			      bool iova_aligned)
{
	dma_unmap_sg(dev, sg, nents, dir);
}

#if defined(CONFIG_IOMMU_API) && defined(CONFIG_NO_GKI) && !defined(CONFIG_ARM)

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
struct iommu_group {
	struct kobject kobj;
	struct kobject *devices_kobj;
	struct list_head devices;
#ifdef __ANDROID_COMMON_KERNEL__
	struct xarray pasid_array;
#endif
	struct mutex mutex;
	void *iommu_data;
	void (*iommu_data_release)(void *iommu_data);
	char *name;
	int id;
	struct iommu_domain *default_domain;
	struct iommu_domain *blocking_domain;
	struct iommu_domain *domain;
	struct list_head entry;
	unsigned int owner_cnt;
	void *owner;
};
#else
struct iommu_group {
	struct kobject kobj;
	struct kobject *devices_kobj;
	struct list_head devices;
	struct mutex mutex;
	struct blocking_notifier_head notifier;
	void *iommu_data;
	void (*iommu_data_release)(void *iommu_data);
	char *name;
	int id;
	struct iommu_domain *default_domain;
	struct iommu_domain *domain;
	struct list_head entry;
};
#endif

int rknpu_iommu_init_domain(struct rknpu_device *rknpu_dev)
{
	// init domain 0
	if (!rknpu_dev->iommu_domains[0]) {
		rknpu_dev->iommu_domain_id = 0;
		rknpu_dev->iommu_domains[rknpu_dev->iommu_domain_id] =
			iommu_get_domain_for_dev(rknpu_dev->dev);
		rknpu_dev->iommu_domain_num = 1;
	}
	return 0;
}

int rknpu_iommu_switch_domain(struct rknpu_device *rknpu_dev, int domain_id)
{
	struct iommu_domain *src_domain = NULL;
	struct iommu_domain *dst_domain = NULL;
	const struct bus_type *bus = NULL;
	int src_domain_id = 0;
	int ret = -EINVAL;

	if (!rknpu_dev->iommu_en)
		return -EINVAL;

	if (domain_id < 0 || domain_id > (RKNPU_MAX_IOMMU_DOMAIN_NUM - 1)) {
		LOG_DEV_ERROR(
			rknpu_dev->dev,
			"invalid iommu domain id: %d, reuse domain id: %d\n",
			domain_id, rknpu_dev->iommu_domain_id);
		return -EINVAL;
	}

	bus = rknpu_dev->dev->bus;
	if (!bus)
		return -EFAULT;

	src_domain_id = rknpu_dev->iommu_domain_id;
	if (domain_id == src_domain_id) {
		return 0;
	}

	src_domain = iommu_get_domain_for_dev(rknpu_dev->dev);
	if (src_domain != rknpu_dev->iommu_domains[src_domain_id]) {
		LOG_DEV_ERROR(
			rknpu_dev->dev,
			"mismatch domain get from iommu_get_domain_for_dev\n");
		return -EINVAL;
	}

	dst_domain = rknpu_dev->iommu_domains[domain_id];
	if (dst_domain != NULL) {
		iommu_detach_device(src_domain, rknpu_dev->dev);
		ret = iommu_attach_device(dst_domain, rknpu_dev->dev);
		if (ret) {
			LOG_DEV_ERROR(
				rknpu_dev->dev,
				"failed to attach dst iommu domain, id: %d, ret: %d\n",
				domain_id, ret);
			if (iommu_attach_device(src_domain, rknpu_dev->dev)) {
				LOG_DEV_ERROR(
					rknpu_dev->dev,
					"failed to reattach src iommu domain, id: %d\n",
					src_domain_id);
			}
			return ret;
		}
		rknpu_dev->iommu_domain_id = domain_id;
	} else {
		dst_domain = iommu_domain_alloc(bus);
		if (!dst_domain) {
			LOG_DEV_ERROR(rknpu_dev->dev,
				      "failed to allocate iommu domain\n");
			return -EIO;
		}
		// init domain iova_cookie
		iommu_get_dma_cookie(dst_domain);

		iommu_detach_device(src_domain, rknpu_dev->dev);
		ret = iommu_attach_device(dst_domain, rknpu_dev->dev);
		if (ret) {
			LOG_DEV_ERROR(
				rknpu_dev->dev,
				"failed to attach iommu domain, id: %d, ret: %d\n",
				domain_id, ret);
			iommu_domain_free(dst_domain);
			return ret;
		}

		// set domain type to dma domain
		dst_domain->type |= __IOMMU_DOMAIN_DMA_API;
		// iommu dma init domain
#if KERNEL_VERSION(6, 10, 0) > LINUX_VERSION_CODE
		iommu_setup_dma_ops(rknpu_dev->dev, 0, 1ULL << 32);
#endif

		rknpu_dev->iommu_domain_id = domain_id;
		rknpu_dev->iommu_domains[domain_id] = dst_domain;
		rknpu_dev->iommu_domain_num++;
	}

	// reset default iommu domain
	rknpu_dev->iommu_group->default_domain = dst_domain;

	LOG_INFO("switch iommu domain from %d to %d\n", src_domain_id,
		 domain_id);

	return ret;
}

int rknpu_iommu_domain_get_and_switch(struct rknpu_device *rknpu_dev,
				      int domain_id)
{
	unsigned long timeout_jiffies =
		msecs_to_jiffies(RKNPU_SWITCH_DOMAIN_WAIT_TIME_MS);
	unsigned long start = jiffies;
	int ret = -EINVAL;

	while (true) {
		mutex_lock(&rknpu_dev->domain_lock);

		if (domain_id == rknpu_dev->iommu_domain_id) {
			atomic_inc(&rknpu_dev->iommu_domain_refcount);
			mutex_unlock(&rknpu_dev->domain_lock);
			break;
		}

		if (atomic_read(&rknpu_dev->iommu_domain_refcount) == 0) {
			ret = rknpu_iommu_switch_domain(rknpu_dev, domain_id);
			if (ret) {
				LOG_DEV_ERROR(
					rknpu_dev->dev,
					"failed to switch iommu domain, id: %d, ret: %d\n",
					domain_id, ret);
				mutex_unlock(&rknpu_dev->domain_lock);
				return ret;
			}
			atomic_inc(&rknpu_dev->iommu_domain_refcount);
			mutex_unlock(&rknpu_dev->domain_lock);
			break;
		}

		mutex_unlock(&rknpu_dev->domain_lock);

		usleep_range(10, 100);
		if (time_after(jiffies, start + timeout_jiffies)) {
			LOG_DEV_ERROR(
				rknpu_dev->dev,
				"switch iommu domain time out, failed to switch iommu domain, id: %d\n",
				domain_id);
			return -EINVAL;
		}
	}

	return 0;
}

int rknpu_iommu_domain_put(struct rknpu_device *rknpu_dev)
{
	atomic_dec(&rknpu_dev->iommu_domain_refcount);

	return 0;
}

void rknpu_iommu_free_domains(struct rknpu_device *rknpu_dev)
{
	int i = 0;

	if (rknpu_iommu_domain_get_and_switch(rknpu_dev, 0)) {
		LOG_DEV_ERROR(rknpu_dev->dev, "%s error\n", __func__);
		return;
	}

	for (i = 1; i < RKNPU_MAX_IOMMU_DOMAIN_NUM; i++) {
		struct iommu_domain *domain = rknpu_dev->iommu_domains[i];

		if (domain == NULL)
			continue;

		iommu_detach_device(domain, rknpu_dev->dev);
		iommu_domain_free(domain);

		rknpu_dev->iommu_domains[i] = NULL;
	}

	rknpu_iommu_domain_put(rknpu_dev);
}

#else

int rknpu_iommu_init_domain(struct rknpu_device *rknpu_dev)
{
	return 0;
}

int rknpu_iommu_switch_domain(struct rknpu_device *rknpu_dev, int domain_id)
{
	return 0;
}

int rknpu_iommu_domain_get_and_switch(struct rknpu_device *rknpu_dev,
				      int domain_id)
{
	return 0;
}

int rknpu_iommu_domain_put(struct rknpu_device *rknpu_dev)
{
	return 0;
}

void rknpu_iommu_free_domains(struct rknpu_device *rknpu_dev)
{
}

#endif
