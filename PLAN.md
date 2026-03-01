# RKNPU 驱动移植到 Armbian 6.19 主线内核计划

## 1. 项目概述

### 1.1. 目标

将 Rockchip NPU (RKNPU) 驱动从 rockchip-linux/kernel `develop-6.6` 分支移植为独立的 out-of-tree 内核模块，使其能在 OrangePi 3B（RK3566）的 Armbian 主线内核（6.19.x）上运行。

### 1.2. 当前环境

**开发主机（本地）**：

| 项目 | 值 |
|------|-----|
| 系统 | Arch Linux x86_64 |
| 编译器 | Clang/LLVM 21.1.8 |
| 交叉编译 | `ARCH=arm64 LLVM=1`（使用 LLVM 工具链，无需 GCC 交叉编译器） |

**目标设备（远程）**：

| 项目 | 值 |
|------|-----|
| 开发板 | OrangePi 3B v1.1 (RK3566) |
| SSH 地址 | `root@10.114.0.2` |
| 当前内核包 | `linux-image-edge-rockchip64` 26.02.0-trunk-dietpi6 |
| 运行内核版本 | `6.19.0-edge-rockchip64`（包版本 6.19.3，可能需要重启） |
| Linux Headers | **未安装**（需要在设备上安装或从设备提取头文件包到本地） |
| NPU 设备树节点 | **不存在**（主线 DTB 中无 NPU 节点） |
| IOMMU | `ROCKCHIP_IOMMU=y`（已编译入内核） |
| DRM | `DRM_ROCKCHIP=m`（已作为模块编译） |
| accel/rocket | `CONFIG_DRM_ACCEL_ROCKET=m`（已编译但仅支持 RK3588） |
| 可用磁盘 | ~22GB |
| 串口调试 | `minicom -D /dev/ttyUSB0 -b 1500000`（本地执行，设备无法 SSH 时使用） |

### 1.3. 设计决策

- **模块类型**：Out-of-tree 内核模块（不修改/重编内核）
- **内存管理后端**：DRM GEM 模式（`CONFIG_ROCKCHIP_RKNPU_DRM_GEM`）
- **设备树**：DT Overlay（`.dtbo` 文件）
- **基础驱动版本**：rockchip-linux/kernel `develop-6.6` 分支的 v0.9.8 驱动

### 1.4. 已有工作

此前在 `w568w/alarm_repo` 仓库的 `linux-aarch64-rockchip-opi3b-npu-w568w/1003-rknpu.patch` 中，已成功将 rknpu 驱动移植到 6.6.15 内核（作为 in-tree 补丁，修改了 45 个文件）。本次需要将其转化为 out-of-tree 模块并适配 6.19 API 变化。

## 2. 关键技术挑战

### 2.1. Rockchip 私有头文件依赖

rknpu 驱动依赖以下 Rockchip 私有头文件（主线内核中不存在）：

| 头文件 | 用途 | 处理策略 |
|--------|------|----------|
| `<soc/rockchip/rockchip_opp_select.h>` | OPP/DVFS 数据结构 | 提供 stub 头文件（空结构体） |
| `<soc/rockchip/rockchip_system_monitor.h>` | 温控/电源监控 | 提供 stub 头文件 |
| `<soc/rockchip/rockchip_ipa.h>` | IPA 功耗模型 | 提供 stub 头文件 |
| `<soc/rockchip/rockchip_iommu.h>` | `rockchip_iommu_is_enabled()` 等 | **需要自行实现或绕过** |
| `<linux/rk-dma-heap.h>` | Rockchip DMA Heap | 不需要（使用 DRM GEM 模式） |
| `<../drivers/devfreq/governor.h>` | devfreq governor 内部头 | 条件编译排除或提供 stub |

### 2.2. IOMMU 兼容性

- **下游内核**：IOMMU compatible = `"rockchip,iommu-v2"`，导出 `rockchip_iommu_is_enabled()` 等函数
- **主线内核**：IOMMU compatible = `"rockchip,rk3568-iommu"`，**不导出** 额外函数
- **处理策略**：DT overlay 中使用 `"rockchip,rk3568-iommu"`；驱动中对 `rockchip_iommu_is_enabled()` 调用使用条件编译绕过

### 2.3. 内核 API 变化（6.6 → 6.19）

rknpu 驱动已包含多版本兼容宏（`KERNEL_VERSION` 检查），但最高只到 6.12。6.13~6.19 之间可能存在的 API 变化需要排查：

- DRM subsystem API 变化（GEM helper、driver 注册方式等）
- IOMMU API 变化
- devfreq API 变化
- PM runtime API 变化
- `struct device` / `struct platform_device` 等核心结构变化

### 2.4. 功能裁剪

以下功能在 out-of-tree 模块中需要禁用或 stub 化：

- **devfreq/DVFS**：依赖 `rockchip_system_monitor.h` 和 `rockchip_opp_select.h`，建议初期禁用
- **SRAM 支持**：需要 `CONFIG_NO_GKI`，禁用
- **IPA 功耗模型**：stub 化
- **Rockchip 私有 IOMMU 函数**：绕过或自行实现

## 3. 详细实施步骤

### 3.1. 阶段一：环境准备

开发在本地 x86_64 主机上完成，使用 LLVM/Clang 交叉编译，编译产物通过 scp 部署到设备。

#### 3.1.1. 在设备上安装内核头文件并同步到本地

```bash
# 1. 在设备上安装 linux-headers
ssh root@10.114.0.2 "apt update && apt install -y linux-headers-edge-rockchip64"

# 2. 如果内核版本不匹配，先重启
ssh root@10.114.0.2 "reboot"
# 等待重启后检查
ssh root@10.114.0.2 "uname -r"  # 应为 6.19.x-edge-rockchip64

# 3. 将内核头文件同步到本地（用于交叉编译）
KVER=$(ssh root@10.114.0.2 "uname -r")
rsync -a root@10.114.0.2:/lib/modules/$KVER/build/ ./kernel-headers/
```

#### 3.1.2. 确认本地开发环境

本地需要：
- **Clang/LLVM 工具链**：`clang`、`ld.lld`、`llvm-objcopy`、`llvm-ar` 等（Arch Linux 的 `llvm` 包）
- **make**
- **dtc**（设备树编译器，Arch Linux 的 `dtc` 包）
- **scp/rsync**（用于部署到设备）

无需安装 GCC 交叉编译器（`aarch64-linux-gnu-gcc`），LLVM 工具链原生支持交叉编译。

### 3.2. 阶段二：驱动源码准备

#### 3.2.1. 获取上游驱动源码

从 `rockchip-linux/kernel` `develop-6.6` 分支获取 `drivers/rknpu/` 目录的全部文件。

目录结构：

```
rknpu-module/
├── PLAN.md                  # 本文件
├── Makefile                 # Out-of-tree 模块构建 Makefile
├── src/                     # 驱动源码
│   ├── rknpu_drv.c
│   ├── rknpu_reset.c
│   ├── rknpu_job.c
│   ├── rknpu_debugger.c
│   ├── rknpu_iommu.c
│   ├── rknpu_gem.c
│   ├── rknpu_fence.c
│   ├── rknpu_devfreq.c      # 可能需要大幅修改或禁用
│   ├── rknpu_mm.c           # 禁用（SRAM）
│   ├── rknpu_mem.c          # 不编译（DMA Heap 模式）
│   └── include/
│       ├── rknpu_*.h        # 驱动内部头文件
│       └── compat/          # 兼容性 stub 头文件
│           ├── soc/rockchip/rockchip_opp_select.h
│           ├── soc/rockchip/rockchip_system_monitor.h
│           ├── soc/rockchip/rockchip_ipa.h
│           └── soc/rockchip/rockchip_iommu.h
├── overlay/
│   ├── rk3566-rknpu.dts     # DT overlay 源码
│   └── Makefile             # overlay 编译
└── test/
    └── ...                  # 测试脚本
```

#### 3.2.2. 创建 Out-of-tree Makefile（LLVM 交叉编译）

```makefile
# 交叉编译配置：使用 LLVM 工具链编译 arm64 内核模块
ARCH ?= arm64
LLVM ?= 1

# 内核头文件路径：默认使用本地同步的头文件
KDIR ?= $(PWD)/kernel-headers

obj-m := rknpu.o
rknpu-y := src/rknpu_drv.o src/rknpu_reset.o src/rknpu_job.o \
           src/rknpu_debugger.o src/rknpu_iommu.o src/rknpu_gem.o
rknpu-y += src/rknpu_fence.o

ccflags-y += -I$(src)/src/include
ccflags-y += -I$(src)/src/include/compat
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_DRM_GEM
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_DEBUG_FS
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_FENCE

all:
	$(MAKE) ARCH=$(ARCH) LLVM=$(LLVM) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) ARCH=$(ARCH) LLVM=$(LLVM) -C $(KDIR) M=$(PWD) clean

# 部署到设备
DEVICE ?= root@10.114.0.2
KVER ?= $(shell ssh $(DEVICE) "uname -r")

deploy: all
	scp rknpu.ko $(DEVICE):/root/
	@echo "Module deployed. Load with: insmod /root/rknpu.ko"

deploy-overlay:
	scp overlay/rk3566-rknpu.dtbo $(DEVICE):/boot/dtb/rockchip/overlay/
	@echo "Overlay deployed. Add 'user_overlays=rk3566-rknpu' to /boot/dietpiEnv.txt and reboot."
```

### 3.3. 阶段三：源码适配

#### 3.3.1. 创建 stub 头文件

为主线内核中不存在的 Rockchip 私有头文件创建最小 stub：

**`compat/soc/rockchip/rockchip_opp_select.h`**：
- 定义空的 `struct rockchip_opp_info`
- 提供空的内联函数（如 `rockchip_adjust_opp_table()` → 返回 0）

**`compat/soc/rockchip/rockchip_system_monitor.h`**：
- 定义空的 `struct monitor_dev_profile`
- 提供空的内联函数

**`compat/soc/rockchip/rockchip_ipa.h`**：
- 定义空的 `struct ipa_power_model_data`

**`compat/soc/rockchip/rockchip_iommu.h`**：
- `rockchip_iommu_is_enabled()` → 始终返回 true（或检查 IOMMU group）
- `rockchip_iommu_disable()` / `rockchip_iommu_enable()` → 空操作

#### 3.3.2. 修改驱动源码以适配 6.19

需要逐一排查以下文件中的编译错误：

1. **`rknpu_drv.c`**：
   - 移除/条件编译 `rockchip_opp_select` 相关调用
   - 修复 DRM driver 注册方式（6.19 可能有变化）
   - 处理 `FPGA_PLATFORM` 条件编译

2. **`rknpu_gem.c`**：
   - DRM GEM API 在 6.3+ 有变化（`drm_gem_object_funcs`）
   - 检查 `drm_gem_*` helper 函数签名变化

3. **`rknpu_iommu.c`**：
   - 替换 `rockchip_iommu_is_enabled()` 调用
   - IOMMU domain API 在 6.5+ 有变化

4. **`rknpu_devfreq.c`**：
   - **最复杂的文件**：大量依赖 Rockchip 私有 API
   - 方案 A：完全禁用 devfreq（NPU 以默认频率运行）
   - 方案 B：使用标准 devfreq API 重写（工作量大，后期优化）
   - **建议初期采用方案 A**

5. **`rknpu_fence.c`**：DMA fence API 相对稳定，可能不需要大改

6. **`rknpu_job.c`** / **`rknpu_reset.c`** / **`rknpu_debugger.c`**：
   依赖较少的外部私有 API

#### 3.3.3. 迭代编译修复

在本地交叉编译，逐步修复编译错误：

```bash
# 本地交叉编译（LLVM 工具链自动处理 arm64 目标）
make -j$(nproc)
# 修复错误
# 重复直到编译成功

# 编译成功后部署到设备测试
make deploy
```

### 3.4. 阶段四：设备树 Overlay

#### 3.4.1. 创建 DT Overlay 文件

`overlay/rk3566-rknpu.dts`：

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "rockchip,rk3566";

    fragment@0 {
        target-path = "/";
        __overlay__ {
            rknpu: npu@fde40000 {
                compatible = "rockchip,rk3568-rknpu", "rockchip,rknpu";
                reg = <0x0 0xfde40000 0x0 0x10000>;
                interrupts = <GIC_SPI 151 IRQ_TYPE_LEVEL_HIGH>;
                clocks = <&scmi_clk 2>, <&cru CLK_NPU>,
                         <&cru ACLK_NPU>, <&cru HCLK_NPU>;
                clock-names = "scmi_clk", "clk", "aclk", "hclk";
                assigned-clocks = <&cru CLK_NPU>;
                assigned-clock-rates = <600000000>;
                resets = <&cru SRST_A_NPU>, <&cru SRST_H_NPU>;
                reset-names = "srst_a", "srst_h";
                power-domains = <&power RK3568_PD_NPU>;
                iommus = <&rknpu_mmu>;
                status = "okay";
            };

            rknpu_mmu: iommu@fde4b000 {
                compatible = "rockchip,rk3568-iommu";
                reg = <0x0 0xfde4b000 0x0 0x40>;
                interrupts = <GIC_SPI 151 IRQ_TYPE_LEVEL_HIGH>;
                interrupt-names = "rknpu_mmu";
                clocks = <&cru ACLK_NPU>, <&cru HCLK_NPU>;
                clock-names = "aclk", "iface";
                power-domains = <&power RK3568_PD_NPU>;
                #iommu-cells = <0>;
                status = "okay";
            };
        };
    };
};
```

**关键注意事项**：
- 主线 IOMMU compatible = `"rockchip,rk3568-iommu"`（不是下游的 `"rockchip,iommu-v2"`）
- 移除了 `operating-points-v2` 引用（初期禁用 devfreq）
- DT overlay 使用 phandle 引用（`&cru`、`&scmi_clk`、`&power`）要求基础 DTB 包含 `__symbols__` 节点

#### 3.4.2. DT Overlay 的潜在问题

**验证 DTB 是否支持 overlay phandle 引用**：
```bash
dtc -I dtb -O dts /boot/dtb/rockchip/rk3566-orangepi-3b-v1.1.dtb | grep '__symbols__'
```

**如果不支持 overlay phandle 引用**，备选方案：
1. 反编译 DTB → 手动添加节点 → 重新编译替换
2. 直接用数值 phandle 代替符号引用（脆弱但可行）

#### 3.4.3. 编译与部署 DT Overlay

```bash
# 本地编译 overlay
dtc -I dts -O dtb -o overlay/rk3566-rknpu.dtbo overlay/rk3566-rknpu.dts

# 部署到设备
make deploy-overlay

# 在设备上编辑 /boot/dietpiEnv.txt，添加：
ssh root@10.114.0.2
# user_overlays=rk3566-rknpu

# 重启设备
reboot
```

### 3.5. 阶段五：模块加载与测试

#### 3.5.1. 部署并加载模块

```bash
# 本地编译并部署
make deploy

# SSH 到设备进行测试
ssh root@10.114.0.2

# 确保 DT overlay 已生效
ls /proc/device-tree/npu@fde40000/

# 加载模块
insmod /root/rknpu.ko

# 验证
dmesg | grep -i rknpu
ls /dev/dri/
```

#### 3.5.2. 基础功能测试

1. **驱动加载**：`insmod` 成功，`dmesg` 无错误
2. **设备节点**：`/dev/dri/renderDxxx` 存在
3. **NPU 信息**：通过 IOCTL 查询硬件版本
4. **rknn-toolkit2-lite**：运行 Rockchip 的用户空间测试程序

#### 3.5.3. 推理测试

使用 `rknn-toolkit2-lite` 运行一个简单的推理模型验证 NPU 功能正常。

### 3.6. 阶段六：收尾与优化（可选）

- 添加 DKMS 配置，使内核更新时自动重编模块
- 重新启用 devfreq（使用标准 Linux devfreq API）
- 优化 DT overlay（添加 OPP table）
- 提交到 Armbian 构建系统作为补丁

## 4. 风险评估

### 4.1. 高风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| DT overlay 无法解析 phandle 引用 | overlay 无法使用 | 退回到修改 DTB 方案 |
| DRM GEM API 在 6.19 中有重大变化 | 编译失败 | 参照 `accel/rocket` 驱动的实现方式适配 |
| `rockchip_iommu_is_enabled()` 替代方案不可行 | IOMMU 无法正常工作 | 使用标准 IOMMU API 或 iommu_group 检查 |

### 4.2. 中等风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| devfreq 禁用导致 NPU 性能低 | NPU 以默认（可能是低）频率运行 | 后期添加标准 devfreq 支持 |
| 内核版本不匹配（6.19.0 vs 6.19.3） | 模块加载失败 | 重启开发板使用新内核 |
| 用户空间 rknn 库版本不兼容 | 推理失败 | 确保使用匹配驱动版本的用户空间库 |

## 5. 参考资料

- rockchip-linux/kernel `develop-6.6` 分支：`drivers/rknpu/`
- 之前的移植补丁：`w568w/alarm_repo` → `1003-rknpu.patch`（基于 6.6.15）
- Armbian 构建系统：`armbian/build`
- DietPi issue #7301：NPU 支持讨论
- `accel/rocket` 驱动（`drivers/accel/rocket/`）：主线 NPU 驱动参考实现（仅 RK3588）
- Armbian 内核配置中 `CONFIG_DRM_ACCEL_ROCKET=m` 已启用

## 6. 时间估计

| 阶段 | 预计时间 |
|------|----------|
| 环境准备（安装头文件、同步到本地） | 0.5h |
| 驱动源码获取与目录结构 | 1h |
| stub 头文件创建 | 2h |
| 源码适配与交叉编译修复 | 4-8h（最主要的工作量） |
| DT overlay 创建与调试 | 1-2h |
| 部署到设备、模块加载与测试 | 1-2h |
| **总计** | **~10-16h** |

## 7. 实际执行记录

以下记录了未直接体现在本项目源码文件中的修改操作。

### 7.1. kernel-headers vermagic 修正

设备上运行的内核版本为 `6.19.3-edge-rockchip64`，但从设备同步的 kernel-headers 中 `UTS_RELEASE` 仅为 `6.19.3`，导致编译出的模块 vermagic 不匹配。需要手动修正以下两个文件：

- `kernel-headers/include/generated/utsrelease.h`：

```c
#define UTS_RELEASE "6.19.3-edge-rockchip64"
```

- `kernel-headers/include/config/kernel.release`：

```
6.19.3-edge-rockchip64
```

### 7.2. 设备树 DTB 修改

mainline 内核 DTB 不包含 NPU 设备节点，且 DTB 中没有 `__symbols__` 节点，无法使用标准 DT Overlay phandle 引用。因此采用**直接修改 DTB** 的方案：反编译原始 DTB → 添加节点 → 重编译替换。

原始 DTB 已备份在设备上：`/boot/dtb/rockchip/rk3566-orangepi-3b-v1.1.dtb.bak`

#### 7.2.1. 关键 phandle 映射

从反编译的 DTB 中提取的 phandle 值：

| 节点 | phandle | 说明 |
|------|---------|------|
| `clock-controller@fdd20000`（CRU） | `0x0f` | RK3568 CRU 时钟控制器 |
| `protocol@14`（SCMI CLK） | `0x02` | SCMI 时钟协议节点 |
| `power-controller`（PMU） | `0x11` | 电源域控制器 |
| `qos@fe180000`（QoS NPU） | `0xfc` | NPU QoS 节点 |
| `iommu@fde4b000`（新增） | `0x266` | NPU IOMMU（新分配的 phandle） |

#### 7.2.2. 时钟/复位 ID 映射

来自 `dt-bindings/clock/rk3568-cru.h`：

| 宏名 | 数值 | 十六进制 | 用途 |
|------|------|----------|------|
| `SCMI_CLK_NPU` | 2 | `0x02` | SCMI 时钟中的 NPU 时钟索引 |
| `CLK_NPU` | 35 | `0x23` | NPU 核心时钟 |
| `HCLK_NPU_PRE` | 37 | `0x25` | NPU HCLK 预分频（power domain 用） |
| `PCLK_NPU_PRE` | 38 | `0x26` | NPU PCLK 预分频（power domain 用） |
| `ACLK_NPU_PRE` | 39 | `0x27` | NPU ACLK 预分频（power domain 用） |
| `ACLK_NPU` | 40 | `0x28` | NPU ACLK |
| `HCLK_NPU` | 41 | `0x29` | NPU HCLK |
| `SRST_A_NPU` | 43 | `0x2b` | NPU ACLK 复位 |
| `SRST_H_NPU` | 44 | `0x2c` | NPU HCLK 复位 |
| `RK3568_PD_NPU` | 6 | `0x06` | NPU 电源域索引 |

#### 7.2.3. 新增的三个 DTS 节点

**1. NPU 电源域**（插入到 `power-controller` 内，`power-domain@7` 之前）：

```dts
power-domain@6 {
    reg = <0x06>;
    clocks = <0x0f 0x27 0x0f 0x25 0x0f 0x26>;
    pm_qos = <0xfc>;
    #power-domain-cells = <0x00>;
};
```

**2. NPU 设备节点**（插入到 `gpu@fde60000` 之前）：

```dts
npu@fde40000 {
    compatible = "rockchip,rk3568-rknpu", "rockchip,rknpu";
    reg = <0x00 0xfde40000 0x00 0x10000>;
    interrupts = <0x00 0x97 0x04>;
    interrupt-names = "npu_irq";
    clocks = <0x02 0x02 0x0f 0x23 0x0f 0x28 0x0f 0x29>;
    clock-names = "scmi_clk", "clk", "aclk", "hclk";
    assigned-clocks = <0x0f 0x23>;
    assigned-clock-rates = <0x23c34600>;
    resets = <0x0f 0x2b 0x0f 0x2c>;
    reset-names = "srst_a", "srst_h";
    power-domains = <0x11 0x06>;
    iommus = <0x266>;
    status = "okay";
};
```

**3. NPU IOMMU 节点**（紧跟 NPU 节点之后）：

```dts
iommu@fde4b000 {
    compatible = "rockchip,rk3568-iommu";
    reg = <0x00 0xfde4b000 0x00 0x40>;
    interrupts = <0x00 0x97 0x04>;
    interrupt-names = "rknpu_mmu";
    clocks = <0x0f 0x28 0x0f 0x29>;
    clock-names = "aclk", "iface";
    power-domains = <0x11 0x06>;
    #iommu-cells = <0x00>;
    phandle = <0x266>;
    status = "okay";
};
```

#### 7.2.4. DTB 重编译与部署命令

```bash
# 1. 从设备获取 DTB 并反编译
ssh root@10.114.0.2 "cat /boot/dtb/rockchip/rk3566-orangepi-3b-v1.1.dtb" > /tmp/device.dtb
dtc -I dtb -O dts /tmp/device.dtb > /tmp/device-modified.dts

# 2. 编辑 /tmp/device-modified.dts，添加上述三个节点

# 3. 重编译（warning 是正常的，反编译 DTS 中数值 phandle 会触发）
dtc -I dts -O dtb -o /tmp/device-modified.dtb /tmp/device-modified.dts

# 4. 部署
cat /tmp/device-modified.dtb | ssh root@10.114.0.2 "cat > /boot/dtb/rockchip/rk3566-orangepi-3b-v1.1.dtb"
```

### 7.3. 已知的非致命日志

模块加载后 dmesg 中有以下非致命信息：

- `error -EBUSY: can't request region for resource [mem 0xfde40000-0xfde4ffff]`：NPU 寄存器范围（64KB）与 IOMMU 寄存器区域重叠，`devm_ioremap_resource` 失败后回退到 `devm_ioremap`，功能不受影响。
- `vdd_npu: disabling`：NPU 空闲时电源管理自动关闭供电，属正常行为。

### 7.4. rknn_init oops 修复（iommu_dma_cookie 结构体变更）

`rknn_init` 调用 `rknpu_gem_object_create` 分配 DMA buffer 时触发 kernel oops：

```
queued_spin_lock_slowpath+0x23c/0x440
_raw_spin_lock_irqsave+0x80/0xa0
alloc_iova+0x84/0x288
rknpu_iommu_dma_alloc_iova+0x8c/0xb0 [rknpu]
rknpu_iommu_dma_map_sg+0x18c/0x330 [rknpu]
```

**根因**：内核 6.15 起，`struct iommu_dma_cookie`（`drivers/iommu/dma-iommu.c`）删除了 `type` 字段，`iovad` 从 offset +8 移至 offset +0。驱动中的 `rknpu_iommu_dma_cookie` 仍保留旧布局，导致 `alloc_iova()` 在错误偏移上解引用 spinlock。

**修复**：`src/include/rknpu_iommu.h` 中添加版本条件编译：

```c
#if KERNEL_VERSION(6, 15, 0) <= LINUX_VERSION_CODE
struct rknpu_iommu_dma_cookie {
	struct iova_domain iovad;
};
#else
enum iommu_dma_cookie_type { ... };
struct rknpu_iommu_dma_cookie {
	enum iommu_dma_cookie_type type;
	struct iova_domain iovad;
};
#endif
```

### 7.5. IOMMU 模式推理失败调试与 non-IOMMU 方案

#### 7.5.1. 问题现象

修复 `rknn_init` oops 后，`rknn_init` 和 `rknn_query` 均正常，但 `rknn_run` 超时失败：

```
E RKNN: failed to submit, op id: 1, op name: Conv:MobilenetV1/MobilenetV1/Conv2d_0/Relu6,
        flags: 0x5, task start: 0, task number: 53, run task counter: 0, int status: 0
```

对应 dmesg：

```
RKNPU: job: ..., mask: 0x1, wait_count: 1, commit elapse time: 6177706us, timeout: 6000000us
RKNPU: failed to wait job, task counter: 0, flags: 0x5, ret = 0
RKNPU: job timeout, flags: 0x0:
RKNPU: 	core 0 irq status: 0x0, raw status: 0x10000, require mask: 0x300,
        task counter: 0x0, elapsed time: 6282325us
RKNPU: soft reset, num: 2
```

关键指标：

| 寄存器 | 值 | 含义 |
|--------|-----|------|
| `irq status` | `0x0` | NPU 未产生完成中断 |
| `raw status` | `0x10000` (bit 16) | 总线/DMA 错误标志 |
| `require mask` | `0x300` (bit 8-9) | 期望的完成中断 mask |
| `task counter` | `0x0` | NPU 从未开始执行任务 |

#### 7.5.2. 调查过程

1. **pm_runtime 与 power domain 状态检查**

   在 `rknpu_power_on` 中添加调试打印，发现 `pm_runtime_get_sync` 返回 0（成功），`runtime_status` 变为 `RPM_ACTIVE`，但 `/sys/kernel/debug/pm_genpd/pm_genpd_summary` 显示 NPU power domain 始终为 `off-0`：

   ```
   rknpu_power_on: pm_runtime_get_sync ret=0, runtime_status=0, usage_count=1
   # 但同时：
   npu                             off-0
       fde40000.npu                    suspended
   ```

   通过 sysfs 手动设置 `echo on > /sys/devices/platform/fde40000.npu/power/control` 可使 power domain 变为 `on`，NPU 寄存器空间可访问（如 `INT_MASK@0x20 = 0x8001ffff`）。

2. **NPU version 寄存器读值为 0**

   无论 power domain 是否开启，`RKNPU_OFFSET_VERSION (0x0)` 和 `RKNPU_OFFSET_VERSION_NUM (0x4)` 均读出 `0x00000000`。经验证这对 RK3566 是**正常**的——该 SoC 的 NPU 不使用这些版本寄存器，硬件版本始终返回 0。（此前 `test_npu.py` 的 IOCTL 测试报告 `HW Version=0x00000000` 也是因此。）

3. **Power domain 开启后寄存器扫描**

   在 power domain 强制 on 后扫描 `0xfde40000` 区域，发现非零值：

   | 偏移 | 值 | 含义 |
   |------|-----|------|
   | `0x0010` | `0x00000001` | PC_DATA_ADDR |
   | `0x0018` | `0x0000000f` | - |
   | `0x0020` | `0x8001ffff` | INT_MASK |
   | `0x003c` | `0x00005000` | - |

   IOMMU 区域 `0xfde4b000` 也有正常寄存器数据。这确认**硬件已上电、时钟正常**。

4. **参考下游补丁对比**

   拉取 `w568w/alarm_repo` 的 `1003-rknpu.patch`（基于 6.6.15 的工作版本，驱动 v0.9.3）进行对比：

   | 对比项 | 下游补丁 (v0.9.3) | 当前代码 (v0.9.8) |
   |--------|-------------------|-------------------|
   | IOMMU 管理 | 简单，无 domain 切换 | 完整 domain 切换（`CONFIG_NO_GKI` 守护） |
   | `iommu_dma_cookie` | 有 `type` 字段 | 6.15+ 条件编译去除 `type` |
   | `rknpu_iommu_dma_alloc_iova` | 无 `size_aligned` 参数 | 有 `bool size_aligned` |
   | devfreq | 编译 `rknpu_devfreq.c` | stub（`RKNPU_NO_DEVFREQ`） |
   | DT IOMMU compatible | `rockchip,iommu-v2` | `rockchip,rk3568-iommu` |

   关键差异集中在 IOMMU 子系统的内部 DMA 地址映射逻辑上。v0.9.8 的 `rknpu_iommu_dma_map_sg` 使用私有的 IOVA 分配器直接操作 `iommu_dma_cookie` 内部结构，而主线 6.19 内核的 `iommu-dma` 层已经发生了多处变化（`iommu_dma_cookie` 布局、IOVA 分配策略等），可能导致映射出的 IOVA 地址在 NPU 侧翻译失败。

5. **禁用 IOMMU 验证**

   修改 DTB，从 NPU 节点删除 `iommus` 属性，将 IOMMU 节点设为 `status = "disabled"`。重启后驱动使用 non-IOMMU 模式（物理地址直接 DMA）：

   ```
   RKNPU fde40000.npu: RKNPU: rknpu iommu device-tree entry not found!, using non-iommu mode
   [drm] Initialized rknpu 0.9.8 for fde40000.npu on minor 2
   ```

   **推理立即成功**：

   ```
   [8] rknn_run OK — inference time: 5.63 ms
   --- Performance benchmark (10 runs) ---
     Average: 5.59 ms
   === Inference test PASSED ===
   ```

#### 7.5.3. 结论

| 模式 | `rknn_init` | `rknn_run` | 说明 |
|------|-------------|------------|------|
| IOMMU 启用 | OK | FAIL（6s 超时） | `raw_status=0x10000`，IOVA 映射错误导致 NPU 总线错误 |
| IOMMU 禁用 | OK | OK（~5.6ms） | 直接物理 DMA，绕过 IOMMU 翻译 |

**根因**：驱动 v0.9.8 内部的私有 IOMMU/IOVA 管理代码（`rknpu_iommu_dma_alloc_iova`、`rknpu_iommu_dma_map_sg`）直接操作内核内部结构体 `iommu_dma_cookie`，与主线 6.19 内核的 `iommu-dma` 子系统不兼容。NPU 通过 IOMMU 翻译后的地址无法正确访问 DMA buffer，硬件报告总线错误。

**当前方案**：使用 non-IOMMU 模式运行。在 DTB 中不为 NPU 配置 IOMMU。

#### 7.5.4. 当前 DTB 中 NPU 节点（无 IOMMU）

```dts
npu@fde40000 {
    compatible = "rockchip,rk3568-rknpu", "rockchip,rknpu";
    reg = <0x00 0xfde40000 0x00 0x10000>;
    interrupts = <0x00 0x97 0x04>;
    interrupt-names = "npu_irq";
    clocks = <0x02 0x02 0x0f 0x23 0x0f 0x28 0x0f 0x29>;
    clock-names = "scmi_clk", "clk", "aclk", "hclk";
    assigned-clocks = <0x0f 0x23>;
    assigned-clock-rates = <0x23c34600>;
    resets = <0x0f 0x2b 0x0f 0x2c>;
    reset-names = "srst_a", "srst_h";
    power-domains = <0x11 0x06>;
    rknpu-supply = <0xdc>;
    status = "okay";
};

iommu@fde4b000 {
    compatible = "rockchip,rk3568-iommu";
    ...
    status = "disabled";
};
```

与 §7.2.3 版本的差异：NPU 节点删除了 `iommus = <0x266>;`，IOMMU 节点改为 `status = "disabled"`。

#### 7.5.5. 后续可选优化：修复 IOMMU 模式

若需恢复 IOMMU 支持（节省物理连续内存），需要：

1. 重写 `rknpu_iommu_dma_alloc_iova` / `rknpu_iommu_dma_map_sg`，不再直接操作 `iommu_dma_cookie` 内部结构，改用公开的 `iommu_map` / `iommu_unmap` API 或 `dma_map_sgtable`
2. 或降级到 v0.9.3 驱动代码（IOMMU 逻辑更简单），再适配 6.19 API 变化
3. 验证主线 `rockchip,rk3568-iommu` 驱动与 NPU 的 IOMMU 实例是否完全兼容（下游用 `rockchip,iommu-v2`）

### 7.6. IOMMU 模式调试与根因分析

#### 7.6.1. 驱动侧 IOMMU 适配

将 `rknpu_iommu.c` 中的私有 IOVA 分配代码（直接操作 `iommu_dma_cookie` 内部结构）全部移除，替换为标准内核 API：

- `rknpu_iommu_dma_map_sg` → 无条件调用 `dma_map_sg`
- `rknpu_iommu_dma_unmap_sg` → 无条件调用 `dma_unmap_sg`

DTB 中为 NPU 节点添加 `iommus = <&iommu>` 属性，IOMMU 节点设为 `status = "okay"`。驱动编译加载正常，IOMMU 正确初始化（DTE 被编程，paging 已启用），GEM buffer 获得有效的 IOVA 地址。

#### 7.6.2. 现象

NPU 推理超时（6s），NPU 原始中断状态 `raw_status=0x10000`（bus error，bit 16）。

#### 7.6.3. 调试过程

1. **在 `rknpu_job_subcore_commit_pc()` 中添加调试日志**，打印写入 NPU 寄存器的关键地址：
   - `task_base_addr=0x0`（由用户空间 librknnrt 传入）
   - `first_task->regcmd_addr=0xffc29d40`（有效 IOVA，在 GEM buffer 范围内）

2. **对比非 IOMMU 模式**：`task_base_addr` 在两种模式下都是 0，因此不是问题根因。`PC_DMA_BASE_ADDR` 寄存器在 RK356x（`pc_dma_ctrl=0`）上不被硬件使用。

3. **读取 IOMMU 寄存器**（NPU timeout 时）：
   ```
   DTE_ADDR=0xf0b6b100  → 物理地址 0x1_f0b6b000（RAM segment 2，4GB 以上）
   STATUS=0x00000009     → bit 0: paging enabled, bit 3: idle
   PAGE_FAULT=0x00000000
   INT_RAW=0x00000002    → bit 1: BUS ERROR（不是 page fault）
   ```

4. **`INT_RAW=0x02` 是 bus error**：IOMMU 在读取页表（DTE/PTE）时发生 AXI 总线错误，而非地址翻译失败。

#### 7.6.4. 根因

**RK3566 的 IOMMU v2 硬件无法从 4GB 以上物理地址读取页表。**

设备有 ~4GB RAM，分两段：
- Segment 1: `0x00200000 - 0xefffffff`（~3.7GB，4GB 以下）
- Segment 2: `0x1f0000000 - 0x1ffffffff`（~256MB，4GB 以上）

内核分区：DMA zone（~3.7GB）+ Normal zone（~256MB，4GB 以上）。

主线内核 `drivers/iommu/rockchip-iommu.c` 中，`iommu_data_ops_v2`（用于 `rockchip,rk3568-iommu`）设置：
```c
.dma_bit_mask = DMA_BIT_MASK(40),
.gfp_flags = 0,  // 没有 GFP_DMA32！
```

这允许 IOMMU 页表（通过 `iommu_alloc_page()` + `dma_map_single()`）分配到 Normal zone（4GB 以上）。当页表物理地址为 `0x1f0b6b000` 时，IOMMU 硬件的 AXI master 无法访问该地址，触发 bus error。

**验证**：添加内核启动参数 `mem=3840M` 禁用 Normal zone 后，所有页表分配在 4GB 以下，IOMMU 模式推理正常通过（93.51% Blenheim spaniel）。

#### 7.6.5. 可能的永久修复方案

此问题属于**主线内核 rockchip-iommu 驱动的 bug**（或至少是对 RK3566 的适配缺陷），无法在 out-of-tree 模块层面修复。可选方案：

1. **提交主线内核补丁**：为 RK3566 的 IOMMU 添加 `GFP_DMA32` 标志，或新增一个 `rk3566-iommu` compatible 使用 32-bit DMA mask。需要确认 RK3568 是否也有此问题。
2. **DTB `dma-ranges`**：在 IOMMU 所在的父 bus 节点上添加 `dma-ranges = <0x0 0x0  0x0 0x0  0x1 0x0>` 限制 DMA 到 4GB。但 IOMMU 节点直接在根节点下，加在根节点会影响所有设备。将 IOMMU 包装在独立 simple-bus 中理论可行但会破坏 phandle 引用。
3. **Workaround `mem=3840M`**：启动参数限制可用内存到 4GB 以下。简单有效，但损失 ~256MB RAM。

#### 7.6.6. 当前决策

禁用 IOMMU 模式，使用非 IOMMU 模式运行（DTB 中 NPU 节点无 `iommus` 属性，IOMMU 节点 `status = "disabled"`）。性能无明显差异（非 IOMMU ~7ms vs IOMMU ~16ms，非 IOMMU 反而更快）。
