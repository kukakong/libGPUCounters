# gpu-counter-tool 构建指南

本文档介绍如何从 libGPUCounters 项目源码构建 `gpu-counter-tool` 可执行文件，涵盖 Linux 原生构建和 Android 交叉编译两种场景。

---

## 目录

- [前置条件](#前置条件)
- [CMake 选项说明](#cmake-选项说明)
- [Linux x86_64 原生构建](#linux-x86_64-原生构建)
- [Linux ARM64 原生构建](#linux-arm64-原生构建)
- [Android arm64-v8a 交叉编译](#android-arm64-v8a-交叉编译)
- [Android armeabi-v7a 交叉编译](#android-armeabi-v7a-交叉编译)
- [HWCPIPE_SYSCALL_LIBMALI 选项详解](#hwcpipe_syscall_libmali-选项详解)
- [部署到 Android 设备](#部署到-android-设备)
- [部署到 Linux 目标设备](#部署到-linux-目标设备)
- [运行时依赖说明](#运行时依赖说明)
- [常见问题](#常见问题)

---

## 前置条件

| 依赖项 | 最低版本 | 说明 |
|--------|---------|------|
| CMake | 3.13.5+ | 构建系统，项目 `cmake_minimum_required(VERSION 3.13.5)` |
| C++ 编译器 | 支持 C++14 | 项目使用 `CMAKE_CXX_STANDARD 14`，需支持 C++14 标准 |
| Android NDK | r21+ | 仅 Android 交叉编译时需要 |
| Git | 任意 | 获取源码 |

### 安装 CMake（Linux）

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install cmake

# 或通过 pip 安装最新版
pip install cmake
```

### 安装 C++ 编译器（Linux）

```bash
# Ubuntu/Debian - 安装 GCC
sudo apt install build-essential g++

# 或安装 Clang
sudo apt install clang
```

### 安装 Android NDK

从 [Android NDK 官方页面](https://developer.android.com/ndk/downloads) 下载并解压 NDK，记下解压路径（如 `/opt/android-ndk`），后续构建需要用到。

---

## CMake 选项说明

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `HWCPIPE_BUILD_EXAMPLES` | OFF | **构建示例程序**，必须设为 `ON` 才能构建 `gpu-counter-tool` |
| `HWCPIPE_SYSCALL_LIBMALI` | OFF | 通过 `libmali.so` 的符号转发进行系统调用，而非直接访问 `/dev/mali` 设备节点 |
| `HWCPIPE_WERROR` | ON | 将编译和链接警告视为错误 |
| `HWCPIPE_PIC` | ON | 生成位置无关代码（Position Independent Code） |
| `HWCPIPE_ENABLE_RTTI` | OFF | 启用 C++ RTTI（运行时类型信息） |
| `HWCPIPE_ENABLE_EXCEPTIONS` | OFF | 启用 C++ 异常 |
| `HWCPIPE_WALL` | ON | 启用所有编译警告 |
| `HWCPIPE_ENABLE_LTO` | Release 构建默认 ON | 启用链接时优化（Link Time Optimization） |
| `HWCPIPE_FRONTEND_ENABLE_TESTS` | OFF | 构建单元测试（需要同时启用 RTTI 和异常） |

> **注意**：`HWCPIPE_ENABLE_RTTI` 和 `HWCPIPE_ENABLE_EXCEPTIONS` 默认关闭，因为 hwcpipe 设计用于嵌入游戏引擎，通常不需要 RTTI 和异常。仅在构建单元测试时需要开启。

---

## Linux x86_64 原生构建

适用于在 x86_64 Linux 主机上直接构建并运行（如开发调试用途）。

```bash
# 克隆源码
git clone https://github.com/ARM-software/libGPUCounters.git
cd libGPUCounters

# 配置构建
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DHWCPIPE_BUILD_EXAMPLES=ON \
    -DHWCPIPE_SYSCALL_LIBMALI=OFF \
    -DHWCPIPE_WERROR=OFF \
    .

# 编译
cmake --build build -j$(nproc)

# 构建产物位置
ls build/examples/gpu-counter-tool
```

> **说明**：在 x86_64 主机上构建的产物无法在 ARM 设备上运行。此方式仅用于开发调试或代码验证。

---

## Linux ARM64 原生构建

适用于在 ARM64 Linux 目标设备上直接编译（如搭载 Mali GPU 的 ARM 开发板）。

```bash
# 在 ARM64 设备上执行
git clone https://github.com/ARM-software/libGPUCounters.git
cd libGPUCounters

# 配置构建
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DHWCPIPE_BUILD_EXAMPLES=ON \
    -DHWCPIPE_SYSCALL_LIBMALI=OFF \
    .

# 编译
cmake --build build -j$(nproc)

# 构建产物
ls build/examples/gpu-counter-tool
```

> **说明**：当 `HWCPIPE_SYSCALL_LIBMALI=OFF` 时，程序通过直接访问 `/dev/mali*` 设备节点来采样计数器，需要 root 权限或设备节点访问权限。

---

## Android arm64-v8a 交叉编译

使用项目提供的工具链文件进行 Android arm64-v8a 交叉编译。

```bash
# 设置 NDK 路径（请替换为实际路径）
export ANDROID_NDK=/opt/android-ndk

# 配置构建
cmake -B build-android-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-android-clang.toolchain.cmake \
    -DANDROID_NDK=${ANDROID_NDK} \
    -DANDROID_PLATFORM=android-28 \
    -DCMAKE_BUILD_TYPE=Release \
    -DHWCPIPE_BUILD_EXAMPLES=ON \
    -DHWCPIPE_SYSCALL_LIBMALI=ON \
    .

# 编译
cmake --build build-android-arm64 -j$(nproc)

# 构建产物
ls build-android-arm64/examples/gpu-counter-tool
```

工具链文件 `cmake/toolchains/aarch64-android-clang.toolchain.cmake` 内部设置了 `ANDROID_ABI=arm64-v8a` 和 `ANDROID_TOOLCHAIN_NAME=aarch64-linux-android-clang-`，并引入了 NDK 自带的 `android.toolchain.cmake`。

---

## Android armeabi-v7a 交叉编译

使用项目提供的 32 位 ARM 工具链文件进行交叉编译。

```bash
# 设置 NDK 路径
export ANDROID_NDK=/opt/android-ndk

# 配置构建
cmake -B build-android-armv7 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-android-clang.toolchain.cmake \
    -DANDROID_NDK=${ANDROID_NDK} \
    -DANDROID_PLATFORM=android-28 \
    -DCMAKE_BUILD_TYPE=Release \
    -DHWCPIPE_BUILD_EXAMPLES=ON \
    -DHWCPIPE_SYSCALL_LIBMALI=ON \
    .

# 编译
cmake --build build-android-armv7 -j$(nproc)

# 构建产物
ls build-android-armv7/examples/gpu-counter-tool
```

工具链文件 `cmake/toolchains/arm-android-clang.toolchain.cmake` 内部设置了 `ANDROID_ABI=armeabi-v7a` 和 `ANDROID_TOOLCHAIN_NAME=arm-linux-androideabi-clang-`。

---

## HWCPIPE_SYSCALL_LIBMALI 选项详解

`HWCPIPE_SYSCALL_LIBMALI` 是本项目的关键编译选项，决定了程序与 Mali GPU 驱动交互的方式。

### 工作原理

- **OFF（默认）**：程序使用标准 POSIX 系统调用（`open`、`ioctl`、`mmap` 等）直接访问 `/dev/mali*` 设备节点。这要求程序对设备节点有读写权限（通常需要 root）。
- **ON**：程序通过 `dlopen` 加载 `libmali.so`，使用其中导出的 `mali_open`、`mali_ioctl`、`mali_mmap`、`mali_munmap` 等符号来与驱动交互。这种方式不需要直接访问设备节点，因此不需要 root 权限。

### 选择建议

| 场景 | 推荐值 | 原因 |
|------|--------|------|
| Android 设备（无 root） | `ON` | Android 上普通应用无法访问 `/dev/mali*`，需通过 `libmali.so` 间接访问 |
| Android 设备（有 root） | `ON` 或 `OFF` | 有 root 时两种方式均可，`libmali.so` 方式更通用 |
| Linux ARM 开发板 | `OFF` | Linux 上通常可以直接访问 `/dev/mali*` 设备节点 |
| Linux 服务器（Mali GPU） | `OFF` | 同上，直接访问设备节点更高效 |

### 技术细节

当 `HWCPIPE_SYSCALL_LIBMALI=ON` 时：

1. `device` 库会定义宏 `HWCPIPE_SYSCALL_LIBMALI=1`，并链接 `${CMAKE_DL_LIBS}`（即 `dl` 库）
2. `gpu-counter-tool` 也会定义宏 `HWCPIPE_SYSCALL_LIBMALI=1`
3. 系统调用接口从 `syscall::funcs::unix` 切换为 `syscall::funcs::libmali`
4. `libmali` 类在首次使用时通过 `dlopen("libmali.so", RTLD_NOW)` 加载共享库

---

## 部署到 Android 设备

### 前提条件

- 已通过 adb 连接 Android 设备
- 已完成对应架构的交叉编译

### 部署步骤

```bash
# 确认设备已连接
adb devices

# 推送可执行文件到设备
# arm64-v8a
adb push build-android-arm64/examples/gpu-counter-tool /data/local/tmp/

# armeabi-v7a
adb push build-android-armv7/examples/gpu-counter-tool /data/local/tmp/

# 赋予执行权限
adb shell chmod +x /data/local/tmp/gpu-counter-tool

# 运行
adb shell /data/local/tmp/gpu-counter-tool --list-gpus
adb shell /data/local/tmp/gpu-counter-tool -d 0 -t 10 -o /data/local/tmp/counters.csv

# 拉取采样结果
adb pull /data/local/tmp/counters.csv ./
```

### 注意事项

- `/data/local/tmp/` 是 Android 上少数不需要 root 即可写入的目录
- 如果使用 `HWCPIPE_SYSCALL_LIBMALI=ON` 构建，确保设备上存在 `libmali.so`（通常位于 `/vendor/lib64/` 或 `/vendor/lib/`）
- `gpu-counter-tool` 运行时会自动搜索常见路径下的 `libmali.so` 并预加载
- 如需指定特定路径，可通过 `LD_LIBRARY_PATH` 环境变量指定

---

## 部署到 Linux 目标设备

### 通过 scp 部署

```bash
# 将构建产物传输到目标设备
scp build/examples/gpu-counter-tool user@target-device:/home/user/

# SSH 登录到目标设备
ssh user@target-device

# 赋予执行权限
chmod +x /home/user/gpu-counter-tool

# 运行（HWCPIPE_SYSCALL_LIBMALI=OFF 时可能需要 sudo）
sudo ./gpu-counter-tool --list-gpus
sudo ./gpu-counter-tool -d 0 -t 10 -o counters.csv

# 或在具有 /dev/mali 访问权限时直接运行
./gpu-counter-tool --list-gpus
```

### 设置设备节点权限（可选）

如果不想每次使用 sudo，可以将当前用户加入 `video` 或 `render` 组：

```bash
# 查看设备节点所属组
ls -la /dev/mali*

# 将当前用户加入对应组
sudo usermod -aG video $USER
sudo usermod -aG render $USER

# 重新登录后生效
```

---

## 运行时依赖说明

### 静态链接

`gpu-counter-tool` 采用静态链接方式构建，链接了以下静态库：

- `libdevice.a` — 底层硬件采样后端
- `libhwcpipe.a` — 高层查询和采样前端

因此，部署时**只需传输单个可执行文件**，无需额外携带库文件。

### libmali.so 运行时依赖

当使用 `HWCPIPE_SYSCALL_LIBMALI=ON` 构建时，`gpu-counter-tool` 在运行时需要加载 `libmali.so`：

- **Android**：`libmali.so` 通常已预装在设备上，位于以下路径之一：
  - `/vendor/lib64/egl/libGLES_mali.so`（64 位）
  - `/vendor/lib/egl/libGLES_mali.so`（32 位）
  - `/vendor/lib64/libmali.so`
  - `/vendor/lib/libmali.so`

- **Linux**：`libmali.so` 需由 Mali GPU 驱动包安装，通常位于：
  - `/usr/lib/aarch64-linux-gnu/libmali.so`
  - `/usr/lib/arm-linux-gnueabihf/libmali.so`

> **重要**：如果 `libmali.so` 缺失或版本不兼容，程序将无法启动并报错 `Failed to load libmali.so`。

### 无 libmali 依赖模式

当使用 `HWCPIPE_SYSCALL_LIBMALI=OFF` 构建时，程序无任何运行时库依赖，仅需对 `/dev/mali*` 设备节点有访问权限。

---

## 常见问题

### 1. 编译报错 "CMake 3.13.5 or higher is required"

系统安装的 CMake 版本过低，请升级：

```bash
pip install --upgrade cmake
```

### 2. 编译报错 C++14 相关错误

确保编译器支持 C++14 标准。GCC 5+ 和 Clang 3.4+ 均支持。

### 3. Android 交叉编译找不到 NDK

确保设置了正确的 `ANDROID_NDK` 路径，并传递给 CMake：

```bash
cmake -DANDROID_NDK=/path/to/android-ndk ...
```

### 4. 运行时报错 "Failed to load libmali.so"

- 检查设备上是否存在 `libmali.so`
- 检查架构是否匹配（64 位程序需要 64 位的 `libmali.so`）
- 尝试设置 `LD_LIBRARY_PATH` 指向 `libmali.so` 所在目录

### 5. 运行时报错 "No permission to access /dev/mali"

- 使用 `sudo` 运行
- 或将用户加入 `video`/`render` 组
- 或使用 `HWCPIPE_SYSCALL_LIBMALI=ON` 重新构建

### 6. 运行时报错 "Another process is already using the counters"

Mali GPU 计数器同一时间只能被一个进程占用，请关闭其他使用计数器的程序（如 Streamline 性能分析器）。

### 7. 如何选择 Android API Level

`ANDROID_PLATFORM` 建议设置为 `android-28`（Android 9）或更高。较低的 API Level 可能缺少必要的系统调用支持。
