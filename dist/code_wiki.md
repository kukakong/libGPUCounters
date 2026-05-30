# libGPUCounters Code Wiki

> **项目名称**: libGPUCounters (原 HWCPipe)
> **版本**: 2.4
> **许可证**: MIT
> **版权**: Copyright © 2023-2025, Arm Limited and contributors.

---

## 目录

1. [项目概述](#1-项目概述)
2. [整体架构](#2-整体架构)
3. [目录结构](#3-目录结构)
4. [主要模块职责](#4-主要模块职责)
   - 4.1 [backend 模块（底层硬件采样后端）](#41-backend-模块底层硬件采样后端)
   - 4.2 [hwcpipe 模块（高层查询与采样前端）](#42-hwcpipe-模块高层查询与采样前端)
   - 4.3 [specification 模块（机器可读规范与 Python 工具）](#43-specification-模块机器可读规范与-python-工具)
   - 4.4 [docs 模块（交互式计数器文档）](#44-docs-模块交互式计数器文档)
   - 4.5 [examples 模块（示例程序）](#45-examples-模块示例程序)
   - 4.6 [test 模块（测试）](#46-test-模块测试)
5. [关键类与函数说明](#5-关键类与函数说明)
   - 5.1 [hwcpipe 前端层关键类](#51-hwcpipe-前端层关键类)
   - 5.2 [device 后端层关键类](#52-device-后端层关键类)
   - 5.3 [specification Python 关键类](#53-specification-python-关键类)
6. [依赖关系](#6-依赖关系)
   - 6.1 [C++ 编译依赖](#61-c-编译依赖)
   - 6.2 [Python 依赖](#62-python-依赖)
   - 6.3 [模块间依赖关系图](#63-模块间依赖关系图)
7. [项目构建与运行方式](#7-项目构建与运行方式)
   - 7.1 [构建系统](#71-构建系统)
   - 7.2 [构建步骤](#72-构建步骤)
   - 7.3 [CMake 选项](#73-cmake-选项)
   - 7.4 [集成到现有项目](#74-集成到现有项目)
   - 7.5 [Python 工具使用](#75-python-工具使用)
8. [支持的 GPU 设备](#8-支持的-gpu-设备)
9. [数据流与采样流程](#9-数据流与采样流程)
10. [错误处理机制](#10-错误处理机制)

---

## 1. 项目概述

libGPUCounters 是一个由 Arm 维护的开源工具库，允许应用程序从 Arm Immortalis™ 和 Arm Mali™ GPU 采样性能计数器。开发者可以利用该库对其应用工作负载进行性能分析和优化，并能在应用 UI 中实时显示性能数据。

**2.x 版本**是对该库的重大重写，能够暴露 Arm Streamline 性能分析器中所有公开可访问的性能计数器。此版本与 1.x 系列 API 不兼容，且不再支持 Arm CPU 性能计数器。

### 核心能力

- 检测系统中所有 Arm GPU 设备
- 查询 GPU 硬件特性（核心数、总线宽度等）
- 枚举特定 GPU 支持的性能计数器
- 配置并执行硬件计数器采样
- 读取原始硬件计数器和派生（计算）计数器
- 提供机器可读的计数器规范数据库

---

## 2. 整体架构

libGPUCounters 采用**分层架构**设计，分为前端和后端两层：

```
┌─────────────────────────────────────────────────────────────┐
│                      用户应用 (User Application)             │
├─────────────────────────────────────────────────────────────┤
│                   hwcpipe 前端 (Frontend)                     │
│  ┌──────────┐  ┌──────────────┐  ┌──────────┐  ┌─────────┐ │
│  │   gpu    │  │counter_database│  │ sampler  │  │  error  │ │
│  └────┬─────┘  └──────┬───────┘  └────┬─────┘  └─────────┘ │
│       │               │               │                      │
│  ┌────┴───────────────┴───────────────┴──────────────────┐  │
│  │              detail (内部实现层)                        │  │
│  │  counter_database | counter_definition | expression    │  │
│  │  all_gpu_counters | derived_functions | hwcpipe_double│  │
│  └──────────────────────┬────────────────────────────────┘  │
├─────────────────────────┼───────────────────────────────────┤
│                device 后端 (Backend)                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────────┐  │
│  │  handle  │  │ instance │  │      hwcnt (硬件计数器)    │  │
│  └──────────┘  └──────────┘  │  ┌────────────────────┐  │  │
│                             │  │   sampler (采样器)   │  │  │
│  ┌──────────────────────┐   │  │  ┌──────┐ ┌───────┐ │  │  │
│  │  ioctl (内核接口层)   │   │  │  │manual│ │periodic│ │  │  │
│  │  kbase | kinstr |    │   │  │  └──────┘ └───────┘ │  │  │
│  │  vinstr | kbase_r21  │   │  └────────────────────┘  │  │
│  └──────────────────────┘   │  ┌──────┐  ┌───────────┐ │  │
│                             │  │reader│  │  sample   │ │  │
│  ┌──────────────────────┐   │  └──────┘  └───────────┘ │  │
│  │  syscall (系统调用层) │   │  ┌─────────────────────┐ │  │
│  │  unix | libmali      │   │  │ block_extents 等    │ │  │
│  └──────────────────────┘   │  └─────────────────────┘ │  │
│                             └──────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│              Mali GPU 内核驱动 (/dev/mali0)                  │
└─────────────────────────────────────────────────────────────┘
```

### 架构设计原则

1. **前后端分离**: `hwcpipe` 前端提供用户友好的高级 API，`device` 后端封装底层硬件交互
2. **策略模式**: `sampler` 类通过模板参数 `backend_policy_t` 实现后端策略的可替换性
3. **桥接模式**: 后端通过 `detail::backend` 抽象接口支持多种 ioctl 实现（vinstr、kinstr_prfcnt）
4. **零开销抽象**: 系统调用接口 (`syscall::iface`) 使用空基类优化，产品代码无额外开销
5. **禁用 RTTI/异常**: 面向游戏引擎集成场景，默认禁用 C++ RTTI 和异常

---

## 3. 目录结构

```
/workspace/
├── backend/                    # 底层设备驱动交互后端
│   └── device/                 # device 静态库源码
│       ├── include/device/     # 公共头文件
│       │   ├── api.hpp         # 库导出宏定义
│       │   ├── constants.hpp   # GPU 常量结构体
│       │   ├── handle.hpp      # 设备驱动句柄
│       │   ├── instance.hpp    # 设备实例
│       │   ├── product_id.hpp  # GPU 产品 ID 与家族枚举
│       │   └── hwcnt/          # 硬件计数器子系统
│       │       ├── block_extents.hpp
│       │       ├── block_metadata.hpp
│       │       ├── blocks_view.hpp
│       │       ├── clock_extents.hpp
│       │       ├── features.hpp
│       │       ├── prfcnt_set.hpp
│       │       ├── reader.hpp
│       │       ├── sample.hpp
│       │       └── sampler/    # 采样器接口
│       │           ├── configuration.hpp
│       │           ├── manual.hpp
│       │           └── periodic.hpp
│       └── src/device/         # 实现源码
│           ├── handle.cpp
│           ├── instance.cpp
│           ├── product_id.cpp
│           ├── hwcnt/          # 硬件计数器实现
│           │   ├── reader.cpp
│           │   ├── backend_type.cpp
│           │   ├── sampler/    # 采样器后端实现
│           │   │   ├── detail/ # 采样器抽象后端
│           │   │   ├── base/   # 基础后端模板
│           │   │   ├── kinstr_prfcnt/ # kinstr_prfcnt ioctl 后端
│           │   │   └── vinstr/         # vinstr ioctl 后端
│           │   └── ioctl/      # 内核 ioctl 接口定义
│           │       ├── kbase/
│           │       ├── kbase_pre_r21/
│           │       ├── kinstr_prfcnt/
│           │       └── vinstr/
│           └── syscall/        # 系统调用抽象层
│               ├── iface.hpp
│               └── funcs/
│                   ├── unix.hpp
│                   └── libmali.hpp
├── hwcpipe/                    # 高层前端库
│   ├── include/hwcpipe/        # 公共头文件
│   │   ├── hwcpipe.hpp         # 总包含头
│   │   ├── gpu.hpp             # GPU 设备类
│   │   ├── sampler.hpp         # 采样器与采样配置
│   │   ├── counter_database.hpp# 计数器数据库
│   │   ├── hwcpipe_counter.h   # 计数器枚举定义 (C 兼容)
│   │   ├── error.hpp           # 错误码定义
│   │   ├── types.hpp           # 基础类型定义
│   │   └── detail/             # 内部实现细节
│   │       ├── counter_database.hpp
│   │       ├── internal_types.hpp
│   │       └── assert.hpp
│   └── src/hwcpipe/            # 实现源码
│       ├── error.cpp
│       ├── detail/counter_database.cpp
│       ├── all_gpu_counters.cpp    # 所有 GPU 计数器映射数据
│       ├── all_gpu_counters.hpp
│       ├── counter_metadata.cpp
│       ├── counter_metadata.hpp
│       ├── derived_functions.cpp   # 派生计数器计算函数
│       └── derived_functions.hpp
├── specification/               # 机器可读规范与 Python 工具
│   ├── database/                # XML/YAML 数据库文件
│   │   ├── counterinfo/         # 计数器信息定义
│   │   ├── hardwarelayout/      # 硬件布局定义
│   │   ├── Mali-ArchitectureInfo.xml
│   │   ├── Mali-ProductInfo.xml
│   │   ├── Mali-SemanticGroupInfo.xml
│   │   ├── Mali-SemanticLayout.yaml
│   │   └── Mali-SemanticSectionInfo.xml
│   ├── documentation/           # 开发者文档
│   │   ├── counter_guide.md
│   │   ├── terminology_guide.md
│   │   └── user_guide.md
│   └── lgcpy/                   # Python 包装库
│       ├── __init__.py
│       ├── database.py          # CounterDatabase 便捷入口
│       ├── data/                # 数据加载类
│       │   ├── architectureinfo.py
│       │   ├── counterinfo.py
│       │   ├── hardwarelayout.py
│       │   ├── productinfo.py
│       │   ├── semanticinfo.py
│       │   └── semanticlayout.py
│       └── view/                # 视图类
│           ├── counterview.py
│           ├── hardwareview.py
│           ├── indexedview.py
│           └── semanticview.py
├── docs/                        # 交互式计数器文档 (HTML)
├── examples/                    # 示例程序
│   └── api_example.cpp
├── test/                        # 测试代码
│   ├── hwcpipe/                 # hwcpipe 前端测试
│   │   └── mock/                # Mock 对象
│   ├── counter-sampler.cpp
│   ├── dummy-test.cpp
│   └── main.cpp
├── third_party/                 # 第三方依赖
│   └── catch2/                  # Catch2 测试框架
├── cmake/                       # CMake 辅助脚本
│   ├── Catch.cmake
│   ├── CatchAddTests.cmake
│   └── toolchains/              # 交叉编译工具链
├── CMakeLists.txt               # 根 CMake 配置
└── LICENSE.md                   # MIT 许可证
```

---

## 4. 主要模块职责

### 4.1 backend 模块（底层硬件采样后端）

**职责**: 直接与 Mali GPU 内核驱动交互，提供硬件计数器的底层采样能力。

**核心子模块**:

| 子模块 | 职责 |
|--------|------|
| `handle` | 管理设备文件描述符的生命周期，打开/关闭 `/dev/maliN` |
| `instance` | 创建设备实例，查询 GPU 常量和硬件计数器块布局 |
| `hwcnt/sampler` | 提供手动和周期性两种采样模式 |
| `hwcnt/reader` | 从环形缓冲区读取采样数据 |
| `hwcnt/sample` | 封装单次采样结果及元数据 |
| `ioctl` | 定义与内核驱动交互的 ioctl 命令和数据结构 |
| `syscall` | 抽象系统调用（open/close/mmap/ioctl/poll），支持 Unix 和 libmali 两种实现 |
| `product_id` | GPU 产品识别，将原始 GPU ID 映射为产品枚举 |

**后端类型**:

| 后端类型 | 说明 | 适用场景 |
|----------|------|----------|
| `vinstr` | 使用 vinstr ioctl 接口 | 旧版内核驱动 |
| `vinstr_pre_r21` | 使用 R21 前版本的 vinstr 接口 | R21 之前内核 |
| `kinstr_prfcnt` | 使用 kinstr_prfcnt ioctl 接口 | 新版内核驱动（优先选择） |
| `kinstr_prfcnt_wa` | kinstr_prfcnt 带 workaround | 特定硬件问题规避 |
| `kinstr_prfcnt_bad` | kinstr_prfcnt 有已知问题 | 降级使用 |

后端选择逻辑：通过 `backend_type_discover()` 发现可用后端，再由 `backend_type_select()` 按优先级选择，也可通过环境变量 `HWCPIPE_BACKEND_INTERFACE` 强制指定。

### 4.2 hwcpipe 模块（高层查询与采样前端）

**职责**: 提供用户友好的高级 API，封装底层后端细节，实现计数器查询、配置和采样。

**核心组件**:

| 组件 | 职责 |
|------|------|
| `gpu` | 表示物理 GPU 设备，查询硬件特性 |
| `find_gpus` | 枚举系统中所有可用 GPU 设备 |
| `counter_database` | 查询特定 GPU 支持的计数器及其描述信息 |
| `sampler_config` | 配置采样会话，注册要采集的计数器 |
| `sampler` | 执行采样操作，读取计数器值 |
| `counter_sample` | 存储单次采样的计数器值和时间戳 |
| `error` | 定义库级错误码和错误类别 |

**计数器类型**:

1. **硬件计数器 (Hardware Counter)**: 直接映射到 GPU PMU 寄存器，通过块类型/偏移量/移位值寻址
2. **派生计数器 (Expression Counter)**: 基于硬件计数器和 GPU 常量通过数学表达式计算得出

**内部实现层 (`detail`)**:

| 组件 | 职责 |
|------|------|
| `counter_database` | 内部计数器数据库实现，维护 GPU-计数器映射 |
| `counter_definition` | 计数器定义（硬件地址或表达式函数） |
| `expression::context` | 表达式求值上下文，提供计数器值和 GPU 配置参数 |
| `expression::evaluator` | 派生计数器求值函数指针类型 |
| `hwcpipe_double` | 安全除法包装类，0/0 返回 0 |
| `all_gpu_counters` | 所有 GPU 产品的计数器映射数据 |
| `derived_functions` | 所有派生计数器的计算函数实现 |

### 4.3 specification 模块（机器可读规范与 Python 工具）

**职责**: 提供机器可读的 GPU 性能计数器规范数据库和 Python 访问工具。

**数据层**:

| 数据源 | 格式 | 内容 |
|--------|------|------|
| `Mali-ProductInfo.xml` | XML | GPU 产品信息（名称、架构、别名） |
| `Mali-ArchitectureInfo.xml` | XML | GPU 架构信息 |
| `counterinfo/` | XML | 计数器定义（名称、描述、单位、方程） |
| `hardwarelayout/` | XML | 硬件布局（块类型、寄存器偏移） |
| `Mali-SemanticLayout.yaml` | YAML | 语义布局（推荐展示顺序） |
| `Mali-SemanticSectionInfo.xml` | XML | 语义分组信息 |
| `Mali-SemanticGroupInfo.xml` | XML | 语义分组详细信息 |

**Python 库 (lgcpy)**:

| 类 | 类型 | 职责 |
|----|------|------|
| `CounterDatabase` | 便捷入口 | 加载所有数据源，创建视图，管理缓存 |
| `CounterInfo` | 数据类 | 单个计数器的元数据 |
| `ProductInfo` | 数据类 | 单个 GPU 产品的信息 |
| `HardwareLayout` | 数据类 | 单个 GPU 的硬件布局 |
| `ArchitectureInfo` | 数据类 | GPU 架构信息 |
| `IndexedView` | 视图类 | 非结构化随机访问视图 |
| `HardwareView` | 视图类 | 按硬件内存布局顺序的结构化视图 |
| `SemanticView` | 视图类 | 按推荐展示顺序的结构化视图（Section > Group > Counter） |
| `CounterView` | 视图类 | 单个计数器的解析后视图 |

### 4.4 docs 模块（交互式计数器文档）

**职责**: 提供基于 HTML/CSS/JS 的交互式计数器参考文档网站，覆盖所有支持的 GPU 产品。

**技术栈**: Bootstrap 5.3.8 + jQuery 3.7.1 + Font Awesome 7.1.0 + Lato 字体

### 4.5 examples 模块（示例程序）

**职责**: 演示库的 API 使用方法。

`api_example.cpp` 展示了完整的典型使用流程：
1. 检测并枚举 GPU 设备
2. 创建 GPU 实例并查询硬件信息
3. 查询支持的计数器列表
4. 配置采样器并添加计数器
5. 启动采样 → 循环采样 → 读取值 → 停止采样

### 4.6 test 模块（测试）

**职责**: 验证库的正确性，使用 Catch2 测试框架。

| 测试文件 | 测试内容 |
|----------|----------|
| `counter_enumeration.cpp` | 计数器枚举测试 |
| `gpu_instance.cpp` | GPU 实例测试 |
| `hwcpipe_double.cpp` | hwcpipe_double 安全除法测试 |
| `mock/` | Mock 对象（handle、instance、sampler） |

---

## 5. 关键类与函数说明

### 5.1 hwcpipe 前端层关键类

#### `hwcpipe::gpu`

**文件**: `hwcpipe/include/hwcpipe/gpu.hpp`

表示物理 GPU 设备，提供硬件特性查询。

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `gpu(int device_number)` | - | 构造函数，探测指定设备号的 GPU |
| `get_device_number()` | `int` | 获取设备编号 |
| `num_shader_cores()` | `uint64_t` | 获取 Shader Core 数量 |
| `num_execution_engines()` | `uint64_t` | 获取执行引擎数量 |
| `bus_width()` | `uint64_t` | 获取 AXI 总线宽度（位） |
| `get_product_id()` | `device::product_id` | 获取 GPU 产品 ID |
| `get_gpu_family()` | `device::gpu_family` | 获取 GPU 家族（Midgard/Bifrost/Valhall/5th Gen） |
| `get_constants()` | `device::constants` | 获取 GPU 常量结构体 |
| `valid()` | `bool` | 检查 GPU 实例是否有效 |
| `operator bool()` | - | 隐式转换为 bool，等价于 `valid()` |

#### `hwcpipe::find_gpus`

**文件**: `hwcpipe/include/hwcpipe/gpu.hpp`

可枚举的 GPU 设备视图，扫描 `/dev/mali0` ~ `/dev/mali31`。

```cpp
for (const auto &gpu : hwcpipe::find_gpus()) {
    std::cout << "Found GPU " << gpu.get_device_number() << std::endl;
}
```

#### `hwcpipe::counter_database`

**文件**: `hwcpipe/include/hwcpipe/counter_database.hpp`

提供计数器信息查询。

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `counters_for_gpu(const gpu &)` | 可迭代对象 | 返回指定 GPU 支持的所有计数器 |
| `describe_counter(hwcpipe_counter, counter_metadata &)` | `std::error_code` | 获取计数器的描述信息（名称、单位） |

#### `hwcpipe::sampler_config`

**文件**: `hwcpipe/include/hwcpipe/sampler.hpp`

采样配置构建器。

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `sampler_config(const gpu &)` | - | 为指定 GPU 创建配置 |
| `add_counter(hwcpipe_counter)` | `std::error_code` | 添加要采集的计数器（自动验证和注册依赖） |
| `get_valid_counters()` | `const std::set<registered_counter>&` | 获取已验证的计数器集合 |
| `build_backend_config_list()` | `std::vector<backend_cfg_type>` | 构建后端可用的配置列表 |
| `get_device_number()` | `int` | 获取设备编号 |

#### `hwcpipe::sampler<backend_policy_t>`

**文件**: `hwcpipe/include/hwcpipe/sampler.hpp`

核心采样器类，模板参数默认为 `hwcpipe_backend_policy`。

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `sampler(const sampler_config &)` | - | 从配置构造采样器 |
| `start_sampling()` | `std::error_code` | 启动计数器累积 |
| `stop_sampling()` | `std::error_code` | 停止计数器累积 |
| `sample_now()` | `std::error_code` | 采集当前计数器值到内部缓冲区 |
| `get_counter_value(hwcpipe_counter, counter_sample &)` | `std::error_code` | 读取指定计数器的最近采样值 |
| `sample_view()` | 可迭代对象 | 返回所有已配置计数器的采样视图 |
| `operator bool()` | - | 检查采样器是否有效 |

#### `hwcpipe::counter_sample`

**文件**: `hwcpipe/include/hwcpipe/sampler.hpp`

单次采样结果。

| 字段 | 类型 | 说明 |
|------|------|------|
| `counter` | `hwcpipe_counter` | 计数器 ID |
| `timestamp` | `uint64_t` | 采样时间戳 |
| `value` | `union { uint64_t; double }` | 采样值 |
| `type` | `enum { uint64, float64 }` | 值类型 |

#### `hwcpipe_counter` (枚举)

**文件**: `hwcpipe/include/hwcpipe/hwcpipe_counter.h`

C 兼容的计数器枚举，定义了 433 个计数器，覆盖以下功能域：
- GPU 级别（GPU Active Cycles、GPU Pixels 等）
- Fragment 处理（Overdraw、Warp、Queue 等）
- Geometry 处理（Culling、Visible Primitives 等）
- Shader Core（ALU、Texture、LS、Var 等）
- L2 Cache（Read/Write/Lookup/Miss 等）
- Tiler（Position/Variable Cache 等）
- External Bus（Read/Write/Bytes/Stall 等）
- MMU（L2/L3 Hit/Lookup 等）
- CSF（Command Stream Frontend 各单元利用率）
- Ray Tracing Unit（RTU 相关计数器）

#### `hwcpipe::errc` (错误码枚举)

**文件**: `hwcpipe/include/hwcpipe/error.hpp`

| 错误码 | 值 | 说明 |
|--------|----|------|
| `invalid_device` | 1 | 无效设备 |
| `unknown_counter` | 2 | 未知计数器 |
| `invalid_counter_for_device` | 3 | 计数器不被当前设备支持 |
| `sampler_config_invalid` | 4 | 采样器配置无效 |
| `backend_sampler_failure` | 5 | 后端采样器创建失败 |
| `backend_creation_failed` | 6 | 后端创建失败 |
| `sampling_already_started` | 7 | 采样已在进行中 |
| `sampling_not_started` | 8 | 采样未启动 |
| `sample_collection_failure` | 9 | 采样数据采集失败 |
| `accumulation_start_failed` | 10 | 累积启动失败 |
| `accumulation_stop_failed` | 11 | 累积停止失败 |

### 5.2 device 后端层关键类

#### `device::handle`

**文件**: `backend/device/include/device/handle.hpp`

Mali 设备驱动句柄，管理文件描述符生命周期。

| 静态方法 | 说明 |
|----------|------|
| `create(uint32_t instance_number = 0)` | 打开 `/dev/maliN` |
| `create(const char *device_path)` | 打开指定路径的设备 |
| `from_external_fd(int fd)` | 从外部文件描述符创建（不拥有） |

#### `device::instance`

**文件**: `backend/device/include/device/instance.hpp`

Mali 设备驱动实例，查询 GPU 属性。

| 方法 | 说明 |
|------|------|
| `get_constants()` | 获取 GPU 常量（核心数、总线宽度等） |
| `get_hwcnt_block_extents()` | 获取硬件计数器块布局 |
| `get_hwcnt_clock_extents()` | 获取时钟布局 |
| `create(handle &)` | 静态工厂方法 |

#### `device::constants`

**文件**: `backend/device/include/device/constants.hpp`

GPU 硬件常量结构体。

| 字段 | 类型 | 说明 |
|------|------|------|
| `gpu_id` | `uint64_t` | GPU ID |
| `fw_version` | `uint64_t` | CSF 固件版本 |
| `axi_bus_width` | `uint64_t` | AXI 总线宽度（位） |
| `num_shader_cores` | `uint64_t` | Shader Core 数量 |
| `shader_core_mask` | `uint64_t` | Shader Core 掩码 |
| `num_l2_slices` | `uint64_t` | L2 Cache 切片数 |
| `l2_slice_size` | `uint64_t` | L2 Cache 切片大小（字节） |
| `num_exec_engines` | `uint64_t` | 执行引擎数量 |
| `tile_size` | `uint64_t` | Tile 大小（像素） |
| `warp_width` | `uint64_t` | Warp 宽度 |

#### `device::product_id` (枚举)

**文件**: `backend/device/include/device/product_id.hpp`

GPU 产品标识枚举，覆盖 4 个家族：

| 家族 | 产品 |
|------|------|
| Midgard | t60x, t62x, t720, t760, t820, t830, t860, t880 |
| Bifrost | g31, g51, g52, g71, g72, g76 |
| Valhall | g57, g57_2, g68, g77, g78, g78ae, g310, g510, g610, g615, g710, g715 |
| 5th Gen | g720, g620, g725, g625, g1_ultra, g1_premium, g1_pro |

#### `device::hwcnt::sampler::manual`

**文件**: `backend/device/include/device/hwcnt/sampler/manual.hpp`

手动采样器，用户主动触发采样。

| 方法 | 说明 |
|------|------|
| `accumulation_start()` | 启动计数器累积 |
| `accumulation_stop(user_data)` | 停止累积并存储采样 |
| `request_sample(user_data)` | 请求一次采样（不停止累积） |
| `get_reader()` | 获取采样数据读取器 |

#### `device::hwcnt::sampler::periodic`

**文件**: `backend/device/include/device/hwcnt/sampler/periodic.hpp`

周期性采样器，按指定时间间隔自动采样。

| 方法 | 说明 |
|------|------|
| `sampling_start(user_data)` | 启动周期性采样 |
| `sampling_stop(user_data)` | 停止周期性采样 |
| `get_reader()` | 获取采样数据读取器 |

#### `device::hwcnt::reader`

**文件**: `backend/device/include/device/hwcnt/reader.hpp`

硬件计数器读取器，从环形缓冲区读取采样数据。

| 方法 | 说明 |
|------|------|
| `get_fd()` | 获取文件描述符（用于 poll） |
| `get_features()` | 获取后端支持的功能特性 |
| `get_block_extents()` | 获取块布局信息 |
| `get_sample(sample_metadata &, sample_handle &)` | 等待并获取一个采样 |
| `next(sample_handle, block_metadata &, block_handle &)` | 迭代采样中的块 |
| `put_sample(sample_handle)` | 将采样放回环形缓冲区 |
| `discard()` | 丢弃环形缓冲区内容 |

#### `device::hwcnt::sample`

**文件**: `backend/device/include/device/hwcnt/sample.hpp`

采样数据封装，RAII 管理采样生命周期。

| 方法 | 说明 |
|------|------|
| `get_metadata()` | 获取采样元数据 |
| `blocks()` | 获取块视图用于迭代 |

#### `device::hwcnt::block_type` (枚举)

**文件**: `backend/device/include/device/hwcnt/block_metadata.hpp`

| 值 | 说明 |
|----|------|
| `fe` | Front End（前端） |
| `tiler` | Tiler（分块器） |
| `memory` | Memory System（内存系统） |
| `core` | Shader Core（着色器核心） |
| `firmware` | CSF Firmware（CSF 固件） |
| `csg` | Firmware Command Stream Group（固件命令流组） |

#### `device::hwcnt::sampler::configuration`

**文件**: `backend/device/include/device/hwcnt/sampler/configuration.hpp`

每块类型的计数器配置。

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | `block_type` | 块类型 |
| `set` | `prfcnt_set` | 性能计数器集 |
| `enable_map` | `bitset<128>` | 计数器启用位掩码 |

#### `device::syscall::iface`

**文件**: `backend/device/src/device/syscall/iface.hpp`

系统调用抽象接口，封装 open/close/mmap/munmap/ioctl/poll。默认使用 Unix 系统调用，编译时可通过 `HWCPIPE_SYSCALL_LIBMALI` 宏切换为 libmali 实现。设计为空基类以实现零开销抽象。

### 5.3 specification Python 关键类

#### `CounterDatabase`

**文件**: `specification/lgcpy/database.py`

便捷入口类，加载所有数据源并创建视图。

| 方法 | 说明 |
|------|------|
| `get_supported_gpus()` | 获取所有支持的 GPU 列表 |
| `get_indexed_view_for(product_name)` | 获取指定 GPU 的索引视图 |
| `get_hardware_view_for(product_name)` | 获取指定 GPU 的硬件布局视图 |
| `get_semantic_view_for(product_name)` | 获取指定 GPU 的语义视图 |
| `get_architecture_info_for(product_name)` | 获取指定 GPU 的架构信息 |
| `get_product_info_for(product_name)` | 获取指定 GPU 的产品信息 |
| `clear_cache()` | 清除视图缓存 |

---

## 6. 依赖关系

### 6.1 C++ 编译依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| CMake | ≥ 3.13.5 | 构建系统 |
| C++ 编译器 | 支持 C++14 | 编译器要求 |
| Mali GPU 内核驱动 | - | 运行时依赖，提供 `/dev/maliN` 设备节点 |
| Catch2 | (vendored) | 测试框架，仅测试构建时需要 |

### 6.2 Python 依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| lark-parser | 0.12.0 | 方程语法解析 |
| Markdown | 3.10 | 文档生成 |
| PyYAML | 6.0.2 | YAML 文件解析 |
| pytest | 9.0.1 | 测试框架 |
| mypy | 1.18.2 | 类型检查 |
| pycodestyle | 2.14.0 | 代码风格检查 |
| pylint | 4.0.2 | 代码质量检查 |

### 6.3 模块间依赖关系图

```
examples/api_example
    ├── hwcpipe (前端库)
    │   ├── gpu
    │   ├── counter_database
    │   ├── sampler
    │   │   └── sampler_config
    │   └── detail
    │       ├── counter_database (内部)
    │       ├── all_gpu_counters
    │       └── derived_functions
    └── device (后端库)
        ├── handle
        ├── instance
        ├── product_id
        └── hwcnt
            ├── sampler (manual/periodic)
            ├── reader
            ├── sample
            └── ioctl (vinstr/kinstr_prfcnt)

test/
    ├── hwcpipe 测试
    │   └── mock/ (mock handle, instance, sampler)
    └── device 测试
        └── catch2

specification/lgcpy
    ├── data/ (XML/YAML 加载)
    │   ├── counterinfo
    │   ├── productinfo
    │   ├── hardwarelayout
    │   ├── architectureinfo
    │   ├── semanticinfo
    │   └── semanticlayout
    └── view/ (视图构建)
        ├── indexedview ← data
        ├── hardwareview ← indexedview + data
        └── semanticview ← indexedview + data
```

**CMake 目标依赖**:

```
hwcpipe (共享库) ──依赖──> device (静态库)
examples/api_example ──依赖──> hwcpipe + device
test/hwcpipe_tests ──依赖──> hwcpipe + device + catch2
```

---

## 7. 项目构建与运行方式

### 7.1 构建系统

- **构建工具**: CMake ≥ 3.13.5
- **语言标准**: C++14
- **编译选项**: 默认启用 `-Werror`、`-fno-exceptions`、`-fno-rtti`、位置无关代码

### 7.2 构建步骤

```bash
# 基本构建
cmake -B build .
cmake --build build

# 构建示例
cmake -DHWCPIPE_BUILD_EXAMPLES=ON -B build .
cmake --build build

# 构建测试（需要启用 RTTI 和异常）
cmake -DHWCPIPE_FRONTEND_ENABLE_TESTS=ON \
      -DHWCPIPE_ENABLE_EXCEPTIONS=ON \
      -DHWCPIPE_ENABLE_RTTI=ON \
      -B build .
cmake --build build
cd build && ctest

# 交叉编译 Android
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-android-clang.toolchain.cmake \
      -DANDROID_NDK=/path/to/ndk \
      -B build .
cmake --build build
```

### 7.3 CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `HWCPIPE_WALL` | ON | 启用所有警告 |
| `HWCPIPE_WERROR` | ON | 将警告视为错误 |
| `HWCPIPE_PIC` | ON | 生成位置无关代码 |
| `HWCPIPE_ENABLE_RTTI` | OFF | 启用 C++ RTTI |
| `HWCPIPE_ENABLE_EXCEPTIONS` | OFF | 启用 C++ 异常 |
| `HWCPIPE_FRONTEND_ENABLE_TESTS` | OFF | 构建前端测试（需 RTTI+异常） |
| `HWCPIPE_BUILD_EXAMPLES` | OFF | 构建示例程序 |
| `HWCPIPE_ENABLE_LTO` | Release 自动 ON | 启用链接时优化 |
| `HWCPIPE_SYSCALL_LIBMALI` | OFF | 使用 libmali 系统调用实现 |

### 7.4 集成到现有项目

```cmake
add_subdirectory(path/to/libGPUCounters)

target_link_libraries(
    my_project
        device
        hwcpipe)
```

需要链接两个库：`device`（底层后端）和 `hwcpipe`（高层前端）。

### 7.5 Python 工具使用

```bash
cd specification
pip install -r requirements.txt

# 运行测试
./lgcpy_test.sh

# 代码检查
./lgcpy_lint.sh

# 数据验证
python lgcpy_validate.py

# 导出文档
python lgcpy_export_docs_github.py
```

Python 使用示例：

```python
from lgcpy import CounterDatabase

db = CounterDatabase()

# 列出所有支持的 GPU
gpus = db.get_supported_gpus()

# 获取语义视图
view = db.get_semantic_view_for("Mali-G715")

# 遍历计数器
for section in view:
    for group in section:
        for counter in group:
            print(f"{counter.name}: {counter.description}")
```

---

## 8. 支持的 GPU 设备

| 架构 | GPU 产品 | 前端类型 |
|------|----------|----------|
| Midgard | T60x, T62x, T720, T760, T820, T830, T860, T880 | JM |
| Bifrost | Mali-G31, G51, G52, G71, G72, G76 | JM |
| Valhall (JM) | Mali-G57, G57_2, G68, G77, G78, G78AE | JM |
| Valhall (CSF) | Mali-G310, G510, G610, G615, G710, G715 | CSF |
| 5th Gen | Mali-G720, G620, G725, G625, G1-Ultra, G1-Premium, G1-Pro | CSF |

**前端类型说明**:
- **JM (Job Manager)**: 传统 Job Manager 前端，使用 kbase/vinstr ioctl
- **CSF (Command Stream Frontend)**: 命令流前端，使用 kinstr_prfcnt ioctl

---

## 9. 数据流与采样流程

### 典型采样流程

```
1. 创建 GPU 实例
   gpu(0) → handle::create(0) → instance::create(handle) → 获取 constants

2. 查询计数器
   counter_database::counters_for_gpu(gpu) → 遍历 all_gpu_counters 映射

3. 配置采样
   sampler_config(gpu)
     → add_counter(MaliGPUActiveCy)   // 硬件计数器: 设置 enable_map
     → add_counter(MaliCoreUtil)       // 派生计数器: 递归注册依赖

4. 创建采样器
   sampler(config)
     → handle::create(device_number)
     → instance::create(handle)
     → backend_type_discover() → backend_type_select()
     → 创建 kinstr_prfcnt/vinstr backend
     → build_sample_buffer_mappings()

5. 采样循环
   sampler.start_sampling()
     → backend.start() → ioctl(START)
   while (running):
     sampler.sample_now()
       → backend.request_sample() → ioctl(SAMPLE)
       → reader.get_sample() → 等待采样就绪
       → fill_sample_buffer() → 遍历块，提取计数器值
     sampler.get_counter_value(counter, sample)
       → 硬件计数器: 直接从缓冲区读取
       → 派生计数器: 调用 evaluator 函数计算
   sampler.stop_sampling()
     → backend.stop() → ioctl(STOP)
```

### 数据流路径

```
GPU PMU 寄存器
    ↓ (内核驱动定期/手动采样)
内核环形缓冲区 (mmap 共享内存)
    ↓ (reader::get_sample)
用户空间 sample 对象 (block_metadata + values)
    ↓ (fill_sample_buffer)
sampler 内部缓冲区 (counter_to_buffer_pos_ 映射)
    ↓ (get_counter_value)
counter_sample 结构体 (counter_id + timestamp + value)
```

---

## 10. 错误处理机制

### 错误码体系

库使用 `std::error_code` 进行错误处理，不使用 C++ 异常（默认配置下）。

**前端错误码** (`hwcpipe::errc`): 覆盖设备、计数器、采样器各类错误场景。

**后端错误码**: 使用 `std::errc` 标准错误码或 `errno` 值，通过 `syscall::iface` 封装。

### 错误传播模式

```cpp
// 每个可能失败的函数都返回 std::error_code
std::error_code ec = config.add_counter(MaliGPUActiveCy);
if (ec) {
    // 处理错误
}

// 采样器构造后检查有效性
auto sampler = hwcpipe::sampler(config);
if (!sampler) {
    // 采样器创建失败
}
```

### 环境变量

| 环境变量 | 说明 |
|----------|------|
| `HWCPIPE_BACKEND_INTERFACE` | 强制指定后端类型（vinstr/kinstr_prfcnt 等） |

---

*本文档由 libGPUCounters Code Wiki 自动生成，基于项目源码分析。*
