obj-m := rknpu.o
rknpu-y := src/rknpu_drv.o src/rknpu_gem.o src/rknpu_fence.o \
           src/rknpu_job.o src/rknpu_reset.o src/rknpu_iommu.o \
           src/rknpu_debugger.o src/rknpu_devfreq.o

ccflags-y += -I$(src)/src/include
ccflags-y += -I$(src)/src/include/compat
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_DRM_GEM
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_DEBUG_FS
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_FENCE
