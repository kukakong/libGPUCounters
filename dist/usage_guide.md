# gpu-counter-tool 使用指南

> libGPUCounters 项目 — Mali GPU 性能计数器采样工具

---

## 目录

1. [快速入门](#1-快速入门)
2. [列出 GPU 和计数器](#2-列出-gpu-和计数器)
3. [采样所有计数器](#3-采样所有计数器)
4. [输出到 CSV 文件](#4-输出到-csv-文件)
5. [基于时长的监控](#5-基于时长的监控)
6. [基于进程名的监控](#6-基于进程名的监控)
7. [基于 PID 的监控](#7-基于-pid-的监控)
8. [Android 平台使用](#8-android-平台使用)
9. [Linux 平台使用](#9-linux-平台使用)
10. [理解输出结果](#10-理解输出结果)
11. [常见问题排查](#11-常见问题排查)
12. [环境变量](#12-环境变量)

---

## 1. 快速入门

`gpu-counter-tool` 是 libGPUCounters 项目提供的命令行工具，用于对 Mali GPU 的硬件性能计数器进行实时采样和记录。

### 基本用法

```bash
# 查看帮助信息
gpu-counter-tool -h

# 列出所有检测到的 GPU 设备
gpu-counter-tool --list-gpus

# 以默认参数采样所有计数器（100ms 间隔，持续运行直到 Ctrl+C）
gpu-counter-tool
```

### 命令行选项一览

| 选项 | 说明 |
|------|------|
| `--list-gpus` | 列出所有检测到的 GPU 设备 |
| `--list-counters` | 列出当前 GPU 支持的所有计数器 |
| `-d, --device N` | 指定 GPU 设备编号（默认：0） |
| `-i, --interval MS` | 采样间隔，单位毫秒（默认：100） |
| `-t, --duration SEC` | 监控时长，单位秒（0 表示持续运行直到 Ctrl+C） |
| `-o, --output FILE` | 将计数器数据写入 CSV 文件 |
| `-p, --process NAME` | 仅在指定进程运行期间进行监控 |
| `--pid PID` | 仅在指定 PID 存活期间进行监控 |
| `-v, --verbose` | 详细输出模式 |
| `-h, --help` | 显示帮助信息 |

---

## 2. 列出 GPU 和计数器

### 列出 GPU 设备

在开始采样之前，建议先确认系统中检测到了哪些 GPU 设备：

```bash
gpu-counter-tool --list-gpus
```

输出示例：

```
GPU Device 0: Mali-G78, /dev/mali0
GPU Device 1: Mali-G57, /dev/mali1
```

如果系统中有多个 GPU，可以使用 `-d` 选项指定设备编号：

```bash
gpu-counter-tool -d 1 --list-counters
```

### 列出支持的计数器

每个 GPU 型号支持的计数器集合可能不同。使用 `--list-counters` 查看当前设备支持的所有计数器：

```bash
gpu-counter-tool --list-counters
```

输出示例：

```
GPU Cycles
GPU Active Cycles
Fragment Active Cycles
Compute Active Cycles
Vertex/Tiler Active Cycles
Fragment Jobs
Compute Jobs
Vertex/Tiler Jobs
...
```

结合 `-v` 选项可以获取更详细的计数器描述信息：

```bash
gpu-counter-tool -v --list-counters
```

---

## 3. 采样所有计数器

不带任何过滤参数运行时，工具将采样当前 GPU 设备支持的所有计数器：

```bash
# 以默认 100ms 间隔采样，持续运行直到按 Ctrl+C
gpu-counter-tool

# 以 50ms 间隔采样
gpu-counter-tool -i 50

# 指定 GPU 设备编号
gpu-counter-tool -d 0 -i 200
```

运行时，计数器数据将实时输出到终端。按 `Ctrl+C` 可停止采样。

### 采样间隔说明

采样间隔（`-i` 选项）决定了两次采样之间的时间间隔：

- **较小的间隔**（如 10ms）：更高的时间分辨率，但可能增加系统开销
- **较大的间隔**（如 500ms）：较低的开销，但可能遗漏短时峰值

建议根据实际场景选择合适的间隔。对于大多数性能分析场景，100ms（默认值）是一个不错的平衡点。

---

## 4. 输出到 CSV 文件

使用 `-o` 选项可以将采样数据写入 CSV 文件，方便后续分析和可视化：

```bash
gpu-counter-tool -o output.csv -t 30
```

### CSV 文件格式

CSV 文件的第一行为表头，后续每行对应一次采样：

```
timestamp_ns,GPU Cycles,GPU Active Cycles,Fragment Active Cycles,...
1704067200000000000,123456789,98765432,54321098,...
1704067200100000000,234567890,187654321,104321098,...
```

**格式说明：**

- **timestamp_ns**：采样时间戳，单位为纳秒
- **后续列**：各计数器的名称，与 `--list-counters` 输出一致
- **数值**：计数器的累计值或增量值

### 示例

```bash
# 采样 60 秒并保存到 CSV
gpu-counter-tool -o gpu_data.csv -t 60

# 以 50ms 间隔采样并保存
gpu-counter-tool -i 50 -o gpu_data.csv -t 30

# 指定设备并保存
gpu-counter-tool -d 1 -o gpu1_data.csv -t 60
```

---

## 5. 基于时长的监控

使用 `-t` 选项可以指定监控的持续时间（单位：秒）：

```bash
# 监控 30 秒后自动停止
gpu-counter-tool -t 30

# 监控 5 分钟（300 秒）
gpu-counter-tool -t 300

# 监控 60 秒并保存到 CSV
gpu-counter-tool -t 60 -o result.csv
```

当 `-t 0` 或未指定 `-t` 选项时，工具将持续运行直到用户按下 `Ctrl+C`。

### 典型场景

```bash
# 对应用启动过程进行 10 秒采样
gpu-counter-tool -t 10 -i 10 -o app_startup.csv

# 对游戏场景进行 5 分钟采样
gpu-counter-tool -t 300 -i 50 -o game_scene.csv
```

---

## 6. 基于进程名的监控

使用 `-p` 选项可以指定一个进程名，工具将仅在该进程运行期间进行监控：

```bash
# 仅在 my_app 进程运行时采样
gpu-counter-tool -p my_app -o my_app_gpu.csv
```

### 工作原理

> **重要说明：** Mali GPU 的性能计数器是 **系统级（system-wide）** 的，而非进程级（per-process）的。这意味着计数器反映的是整个 GPU 的活动状态，无法单独隔离某个进程的 GPU 使用情况。

`-p` 选项的工作方式是：

1. 工具启动后，轮询检查指定名称的进程是否存在
2. 当目标进程出现时，开始采样计数器数据
3. 当目标进程退出时，停止采样并结束

因此，采样数据中包含了 **整个 GPU** 在该时间段内的活动，而不仅仅是目标进程的 GPU 使用。如果需要更精确的进程级分析，建议结合其他工具（如 `perf`）进行综合分析。

### 示例

```bash
# 监控游戏进程运行期间的 GPU 活动
gpu-counter-tool -p com.game.title -o game_gpu.csv

# 配合采样间隔使用
gpu-counter-tool -p my_render_app -i 50 -o render.csv
```

---

## 7. 基于 PID 的监控

使用 `--pid` 选项可以指定一个进程 ID，工具将仅在该 PID 存活期间进行监控：

```bash
# 监控 PID 为 12345 的进程
gpu-counter-tool --pid 12345 -o pid_gpu.csv
```

### 与 `-p` 选项的区别

| 选项 | 输入 | 适用场景 |
|------|------|----------|
| `-p, --process NAME` | 进程名 | 在进程启动前开始监控，等待目标进程出现 |
| `--pid PID` | 进程 ID | 已知进程 PID，监控该进程的存活期间 |

### 典型用法

```bash
# 先找到目标进程的 PID
pidof my_app
# 输出：98765

# 使用 PID 监控
gpu-counter-tool --pid 98765 -o result.csv

# 也可以组合使用
gpu-counter-tool --pid $(pidof my_app) -o result.csv
```

---

## 8. Android 平台使用

### 部署工具

在 Android 设备上使用 `gpu-counter-tool`，需要先将其推送到设备上：

```bash
# 推送工具到设备
adb push gpu-counter-tool /data/local/tmp/

# 添加执行权限
adb shell chmod +x /data/local/tmp/gpu-counter-tool
```

### 运行工具

```bash
# 列出 GPU 设备
adb shell /data/local/tmp/gpu-counter-tool --list-gpus

# 列出计数器
adb shell /data/local/tmp/gpu-counter-tool --list-counters

# 采样 30 秒
adb shell /data/local/tmp/gpu-counter-tool -t 30

# 采样并保存到设备上的 CSV 文件
adb shell /data/local/tmp/gpu-counter-tool -t 30 -o /data/local/tmp/gpu_data.csv

# 将 CSV 文件拉取到本地
adb pull /data/local/tmp/gpu_data.csv ./
```

### 权限说明

在 Android 设备上访问 Mali GPU 计数器通常需要 root 权限：

```bash
# 如果设备已 root，切换到 root
adb root

# 然后运行工具
adb shell /data/local/tmp/gpu-counter-tool --list-counters
```

如果设备未 root，可能需要：

- 使用 `adb shell su -c` 命令（需要设备已安装 su）
- 设置 SELinux 为宽容模式：`adb shell setenforce 0`（需要 root）

### libmali.so 路径

工具在 Android 上会自动搜索以下路径查找 `libmali.so`：

```
/vendor/lib64/egl/libGLES_mali.so
/vendor/lib/egl/libGLES_mali.so
/system/vendor/lib64/egl/libGLES_mali.so
/system/vendor/lib/egl/libGLES_mali.so
```

如果 `libmali.so` 位于非标准路径，请参考 [环境变量](#12-环境变量) 章节。

### 架构自动检测

工具会自动检测设备的 CPU 架构（arm64/arm/x86_64/x86），请确保推送了与设备架构匹配的二进制文件：

```bash
# 查看设备架构
adb shell getprop ro.product.cpu.abi
```

---

## 9. Linux 平台使用

### 基本运行

```bash
# 列出 GPU 设备
gpu-counter-tool --list-gpus

# 列出计数器
gpu-counter-tool --list-counters

# 采样 60 秒
gpu-counter-tool -t 60 -o result.csv
```

### 权限说明

访问 Mali GPU 硬件计数器需要访问 `/dev/mali` 设备节点，通常需要 root 权限：

```bash
# 使用 sudo 运行
sudo gpu-counter-tool --list-counters

# 采样并保存
sudo gpu-counter-tool -t 30 -o result.csv
```

#### 配置非 root 访问

如果希望不使用 `sudo` 运行，可以将当前用户添加到 `render` 或 `video` 组（取决于系统配置）：

```bash
# 查看 /dev/mali 的权限
ls -l /dev/mali*

# 添加用户到 render 组
sudo usermod -aG render $USER

# 重新登录后生效
```

或者使用 udev 规则设置权限：

```bash
# 创建 udev 规则
echo 'KERNEL=="mali*", MODE="0666"' | sudo tee /etc/udev/rules.d/99-mali.rules

# 重新加载 udev 规则
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### libmali.so 路径

工具在 Linux 上会自动搜索以下路径查找 `libmali.so`：

```
/usr/lib64/libmali.so
/usr/lib/libmali.so
/usr/lib/aarch64-linux-gnu/libmali.so
/usr/lib/arm-linux-gnueabihf/libmali.so
```

如果 `libmali.so` 位于非标准路径，请参考 [环境变量](#12-环境变量) 章节。

---

## 10. 理解输出结果

### 终端输出

在终端运行时，工具会实时输出采样数据。每行包含时间戳和各计数器的值：

```
[1704067200.000] GPU Cycles: 123456789  GPU Active Cycles: 98765432  Fragment Jobs: 150
[1704067200.100] GPU Cycles: 234567890  GPU Active Cycles: 187654321  Fragment Jobs: 280
```

### 常见计数器含义

| 计数器名称 | 说明 |
|------------|------|
| GPU Cycles | GPU 总时钟周期数 |
| GPU Active Cycles | GPU 活跃时钟周期数 |
| Fragment Active Cycles | 片段着色阶段活跃周期数 |
| Compute Active Cycles | 计算着色阶段活跃周期数 |
| Vertex/Tiler Active Cycles | 顶点/Tiler 阶段活跃周期数 |
| Fragment Jobs | 片段着色任务数 |
| Compute Jobs | 计算着色任务数 |
| Vertex/Tiler Jobs | 顶点/Tiler 任务数 |

### 性能分析提示

- **GPU 利用率**：`GPU Active Cycles / GPU Cycles` 可反映 GPU 的整体利用率
- **管线阶段分布**：比较 Fragment、Compute、Vertex/Tiler 的活跃周期，可以判断 GPU 负载主要集中在哪个阶段
- **任务数量**：Jobs 计数器反映各阶段提交的任务数，可用于评估工作负载

---

## 11. 常见问题排查

### 未检测到 GPU 设备

**现象：** 运行 `--list-gpus` 时没有输出或提示 "No GPU found"

**可能原因及解决方案：**

1. **权限不足**：确保有访问 `/dev/mali` 设备节点的权限
   ```bash
   # Linux
   sudo gpu-counter-tool --list-gpus

   # Android
   adb root
   adb shell /data/local/tmp/gpu-counter-tool --list-gpus
   ```

2. **GPU 驱动未安装**：确认系统已安装 Mali GPU 驱动
   ```bash
   # Linux 检查
   ls /dev/mali*

   # Android 检查
   adb shell ls /dev/mali*
   ```

3. **设备节点不存在**：某些嵌入式平台可能需要手动加载驱动模块
   ```bash
   sudo modprobe mali
   ```

### 采样器创建失败

**现象：** 提示 "Failed to create sampler" 或 "Sampler creation failed"

**可能原因及解决方案：**

1. **libmali.so 版本不兼容**：确保 `libmali.so` 版本与 GPU 硬件匹配
2. **驱动不支持性能计数器**：某些旧版本驱动可能不支持硬件计数器接口，请升级 GPU 驱动
3. **SELinux 限制**（Android）：尝试设置宽容模式
   ```bash
   adb shell su -c setenforce 0
   ```

### 权限被拒绝

**现象：** 提示 "Permission denied" 访问 GPU 设备

**解决方案：**

- **Linux**：使用 `sudo` 运行，或将用户添加到适当的组（参见 [Linux 平台使用](#9-linux-平台使用)）
- **Android**：使用 `adb root` 或 `su` 获取 root 权限

### libmali.so 未找到

**现象：** 提示 "libmali.so not found" 或 "Could not locate Mali library"

**解决方案：**

1. 确认系统中存在 `libmali.so`：
   ```bash
   # Linux
   find / -name "libmali.so" 2>/dev/null

   # Android
   adb shell find / -name "libmali.so" 2>/dev/null
   ```

2. 如果 `libmali.so` 位于非标准路径，使用环境变量指定（参见 [环境变量](#12-环境变量)）

3. 如果系统使用的是其他名称的 Mali 库（如 `libGLES_mali.so`），可以创建符号链接：
   ```bash
   sudo ln -s /path/to/libGLES_mali.so /usr/lib64/libmali.so
   ```

---

## 12. 环境变量

### HWCPIPE_BACKEND_INTERFACE

使用 `HWCPIPE_BACKEND_INTERFACE` 环境变量可以强制指定后端接口类型，覆盖工具的自动检测逻辑。

```bash
# 强制使用指定后端接口
export HWCPIPE_BACKEND_INTERFACE=mali_profiler

# 然后运行工具
gpu-counter-tool --list-counters
```

### 适用场景

- 自动检测失败时，手动指定后端类型
- 调试和开发时测试不同的后端实现
- 在非标准环境中使用工具

### 使用示例

```bash
# Linux 上强制指定后端
HWCPIPE_BACKEND_INTERFACE=mali_profiler gpu-counter-tool --list-gpus

# Android 上强制指定后端
adb shell HWCPIPE_BACKEND_INTERFACE=mali_profiler /data/local/tmp/gpu-counter-tool --list-gpus
```

> **注意：** 强制指定不兼容的后端类型可能导致工具无法正常工作。建议仅在自动检测失败或开发调试时使用此环境变量。

---

## 附录：完整命令行参考

```
gpu-counter-tool [选项]

选项：
  --list-gpus            列出所有检测到的 GPU 设备
  --list-counters        列出当前 GPU 支持的所有计数器
  -d, --device N         指定 GPU 设备编号（默认：0）
  -i, --interval MS      采样间隔，单位毫秒（默认：100）
  -t, --duration SEC     监控时长，单位秒（0 = 持续运行直到 Ctrl+C）
  -o, --output FILE      将计数器数据写入 CSV 文件
  -p, --process NAME     仅在指定进程运行期间进行监控
  --pid PID              仅在指定 PID 存活期间进行监控
  -v, --verbose          详细输出模式
  -h, --help             显示帮助信息

环境变量：
  HWCPIPE_BACKEND_INTERFACE  强制指定后端接口类型
```
