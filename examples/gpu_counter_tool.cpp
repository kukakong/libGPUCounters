/*
 * Copyright (c) 2023-2025 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * gpu_counter_tool - A comprehensive GPU performance counter monitoring tool
 * that supports Android and Linux platforms, with automatic platform detection,
 * libmali.so path resolution, file output, duration control, and process monitoring.
 */

#include <device/product_id.hpp>
#include <hwcpipe/counter_database.hpp>
#include <hwcpipe/gpu.hpp>
#include <hwcpipe/sampler.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/api-level.h>
#include <android/log.h>
#define LOG_TAG "gpu_counter_tool"
#define TOOL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define TOOL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define TOOL_LOGI(...) fprintf(stdout, __VA_ARGS__), fputc('\n', stdout)
#define TOOL_LOGE(...) fprintf(stderr, __VA_ARGS__), fputc('\n', stderr)
#endif

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

struct tool_config {
    int device_number = 0;
    int interval_ms = 100;
    int duration_sec = 0;
    std::string output_file;
    std::string process_name;
    int pid = 0;
    bool list_counters = false;
    bool list_gpus = false;
    bool verbose = false;
};

struct platform_info {
    bool is_android;
    bool is_64bit;
    int api_level;
    std::string arch;
    std::string libmali_path;
};

static platform_info detect_platform() {
    platform_info info = {};

#if defined(__ANDROID__)
    info.is_android = true;
#else
    info.is_android = false;
#endif

#if defined(__aarch64__) || defined(__x86_64__)
    info.is_64bit = true;
#else
    info.is_64bit = false;
#endif

#if defined(__aarch64__)
    info.arch = "arm64";
#elif defined(__arm__)
    info.arch = "arm";
#elif defined(__x86_64__)
    info.arch = "x86_64";
#elif defined(__i386__)
    info.arch = "x86";
#else
    info.arch = "unknown";
#endif

#if defined(__ANDROID__)
    info.api_level = android_get_device_api_level();
#else
    info.api_level = 0;
#endif

    return info;
}

static bool file_exists(const char *path) {
    struct stat st = {};
    return stat(path, &st) == 0;
}

static bool try_preload_libmali(platform_info &info) {
    std::vector<std::string> search_paths;

    if (info.is_android) {
        if (info.is_64bit) {
            search_paths.push_back("/vendor/lib64/egl/libGLES_mali.so");
            search_paths.push_back("/vendor/lib64/egl/libGLES_mal.so");
            search_paths.push_back("/system/lib64/egl/libGLES_mali.so");
            search_paths.push_back("/vendor/lib64/libmali.so");
            search_paths.push_back("/system/lib64/libmali.so");
            search_paths.push_back("/vendor/lib64/libMali.so");
        } else {
            search_paths.push_back("/vendor/lib/egl/libGLES_mali.so");
            search_paths.push_back("/vendor/lib/egl/libGLES_mal.so");
            search_paths.push_back("/system/lib/egl/libGLES_mali.so");
            search_paths.push_back("/vendor/lib/libmali.so");
            search_paths.push_back("/system/lib/libmali.so");
            search_paths.push_back("/vendor/lib/libMali.so");
        }
    } else {
        if (info.is_64bit) {
            search_paths.push_back("/usr/lib64/libmali.so");
            search_paths.push_back("/usr/lib64/libMali.so");
            search_paths.push_back("/lib64/libmali.so");
            search_paths.push_back("/lib64/libMali.so");
            search_paths.push_back("/usr/lib/aarch64-linux-gnu/libmali.so");
            search_paths.push_back("/usr/lib/aarch64-linux-gnu/libMali.so");
        } else {
            search_paths.push_back("/usr/lib/libmali.so");
            search_paths.push_back("/usr/lib/libMali.so");
            search_paths.push_back("/lib/libmali.so");
            search_paths.push_back("/lib/libMali.so");
            search_paths.push_back("/usr/lib/arm-linux-gnueabihf/libmali.so");
            search_paths.push_back("/usr/lib/arm-linux-gnueabihf/libMali.so");
        }
    }

    for (const auto &path : search_paths) {
        if (file_exists(path.c_str())) {
            void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                info.libmali_path = path;
                TOOL_LOGI("Preloaded libmali from: %s", path.c_str());
                return true;
            } else {
                TOOL_LOGE("Found %s but dlopen failed: %s", path.c_str(), dlerror());
            }
        }
    }

    return false;
}

static bool process_exists_by_pid(int pid) {
    if (pid <= 0) return false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    return file_exists(path);
}

static bool process_exists_by_name(const std::string &name) {
    if (name.empty()) return false;

    for (int i = 1; i < 65536; ++i) {
        char cmdline_path[64];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", i);

        std::ifstream ifs(cmdline_path);
        if (!ifs.is_open()) continue;

        std::string cmdline;
        std::getline(ifs, cmdline, '\0');

        if (cmdline.find(name) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static int find_pid_by_name(const std::string &name) {
    if (name.empty()) return 0;

    for (int i = 1; i < 65536; ++i) {
        char cmdline_path[64];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", i);

        std::ifstream ifs(cmdline_path);
        if (!ifs.is_open()) continue;

        std::string cmdline;
        std::getline(ifs, cmdline, '\0');

        if (cmdline.find(name) != std::string::npos) {
            return i;
        }
    }
    return 0;
}

static const char *gpu_family_name(hwcpipe::device::gpu_family f) {
    switch (f) {
    case hwcpipe::device::gpu_family::midgard: return "Midgard";
    case hwcpipe::device::gpu_family::bifrost: return "Bifrost";
    case hwcpipe::device::gpu_family::valhall: return "Valhall";
    case hwcpipe::device::gpu_family::fifthgen: return "Arm 5th Gen";
    default: return "Unknown";
    }
}

static const char *gpu_frontend_name(hwcpipe::device::gpu_frontend fe) {
    switch (fe) {
    case hwcpipe::device::gpu_frontend::jm: return "Job Manager";
    case hwcpipe::device::gpu_frontend::csf: return "Command Stream Frontend";
    default: return "Unknown";
    }
}

static const char *product_id_name(hwcpipe::device::product_id pid) {
    switch (pid) {
    case hwcpipe::device::product_id::t60x: return "Mali-T60x";
    case hwcpipe::device::product_id::t62x: return "Mali-T62x";
    case hwcpipe::device::product_id::t720: return "Mali-T720";
    case hwcpipe::device::product_id::t760: return "Mali-T760";
    case hwcpipe::device::product_id::t820: return "Mali-T820";
    case hwcpipe::device::product_id::t830: return "Mali-T830";
    case hwcpipe::device::product_id::t860: return "Mali-T860";
    case hwcpipe::device::product_id::t880: return "Mali-T880";
    case hwcpipe::device::product_id::g31: return "Mali-G31";
    case hwcpipe::device::product_id::g51: return "Mali-G51";
    case hwcpipe::device::product_id::g52: return "Mali-G52";
    case hwcpipe::device::product_id::g71: return "Mali-G71";
    case hwcpipe::device::product_id::g72: return "Mali-G72";
    case hwcpipe::device::product_id::g76: return "Mali-G76";
    case hwcpipe::device::product_id::g57: return "Mali-G57";
    case hwcpipe::device::product_id::g57_2: return "Mali-G57 (MP2)";
    case hwcpipe::device::product_id::g68: return "Mali-G68";
    case hwcpipe::device::product_id::g77: return "Mali-G77";
    case hwcpipe::device::product_id::g78: return "Mali-G78";
    case hwcpipe::device::product_id::g78ae: return "Mali-G78AE";
    case hwcpipe::device::product_id::g310: return "Mali-G310";
    case hwcpipe::device::product_id::g510: return "Mali-G510";
    case hwcpipe::device::product_id::g610: return "Mali-G610";
    case hwcpipe::device::product_id::g615: return "Mali-G615";
    case hwcpipe::device::product_id::g710: return "Mali-G710";
    case hwcpipe::device::product_id::g715: return "Mali-G715";
    case hwcpipe::device::product_id::g720: return "Mali-G720";
    case hwcpipe::device::product_id::g620: return "Mali-G620";
    case hwcpipe::device::product_id::g725: return "Mali-G725";
    case hwcpipe::device::product_id::g625: return "Mali-G625";
    case hwcpipe::device::product_id::g1_ultra: return "Mali-G1 Ultra";
    case hwcpipe::device::product_id::g1_premium: return "Mali-G1 Premium";
    case hwcpipe::device::product_id::g1_pro: return "Mali-G1 Pro";
    default: return "Unknown GPU";
    }
}

static void print_platform_info(const platform_info &info, std::ostream &os) {
    os << "============================================================\n"
       << " Platform Information\n"
       << "============================================================\n"
       << "    OS:          " << (info.is_android ? "Android" : "Linux") << "\n"
       << "    Architecture:" << (info.is_64bit ? " 64-bit" : " 32-bit") << " (" << info.arch << ")\n";
    if (info.is_android && info.api_level > 0) {
        os << "    API Level:   " << info.api_level << "\n";
    }
    if (!info.libmali_path.empty()) {
        os << "    libmali:     " << info.libmali_path << "\n";
    } else {
        os << "    libmali:     Not found (using direct /dev/mali access)\n";
    }
    os << std::endl;
}

static void print_gpu_info(const hwcpipe::gpu &gpu, std::ostream &os) {
    os << "------------------------------------------------------------\n"
       << " GPU Device " << gpu.get_device_number() << "\n"
       << "------------------------------------------------------------\n"
       << "    Product:       " << product_id_name(gpu.get_product_id()) << "\n"
       << "    Family:        " << gpu_family_name(gpu.get_gpu_family()) << "\n"
       << "    Frontend:      " << gpu_frontend_name(hwcpipe::device::get_gpu_frontend(gpu.get_product_id())) << "\n"
       << "    Shader Cores:  " << gpu.num_shader_cores() << "\n"
       << "    Exec Engines:  " << gpu.num_execution_engines() << "\n"
       << "    Bus Width:     " << gpu.bus_width() << " bits\n"
       << "    L2 Slices:     " << gpu.get_constants().num_l2_slices << "\n"
       << "    L2 Slice Size: " << gpu.get_constants().l2_slice_size << " bytes\n"
       << "    Tile Size:     " << gpu.get_constants().tile_size << " pixels\n"
       << "    Warp Width:    " << gpu.get_constants().warp_width << "\n"
       << std::endl;
}

static void list_all_counters(const hwcpipe::gpu &gpu, std::ostream &os) {
    hwcpipe::counter_database db;
    hwcpipe::counter_metadata meta;
    int count = 0;

    os << "============================================================\n"
       << " Supported Counters for " << product_id_name(gpu.get_product_id()) << "\n"
       << "============================================================\n";

    for (hwcpipe_counter counter : db.counters_for_gpu(gpu)) {
        auto ec = db.describe_counter(counter, meta);
        if (!ec) {
            os << "    [" << std::setw(3) << (count + 1) << "] " << meta.name;
            if (meta.units && meta.units[0] != '\0') {
                os << " (" << meta.units << ")";
            }
            os << "\n";
            ++count;
        }
    }

    os << "\n    Total: " << count << " counters supported\n" << std::endl;
}

static void print_usage(const char *prog) {
    std::cerr << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --list-gpus          List all detected GPU devices\n"
              << "  --list-counters      List all supported counters for the GPU\n"
              << "  -d, --device N       GPU device number (default: 0)\n"
              << "  -i, --interval MS    Sampling interval in milliseconds (default: 100)\n"
              << "  -t, --duration SEC   Monitoring duration in seconds (0 = until Ctrl+C)\n"
              << "  -o, --output FILE    Write counter data to CSV file\n"
              << "  -p, --process NAME   Monitor only while process NAME is running\n"
              << "      --pid PID        Monitor only while PID is alive\n"
              << "  -v, --verbose        Verbose output\n"
              << "  -h, --help           Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog << " --list-gpus\n"
              << "  " << prog << " --list-counters -d 0\n"
              << "  " << prog << " -d 0 -t 10 -o counters.csv\n"
              << "  " << prog << " -d 0 -p com.example.app -o counters.csv\n"
              << "  " << prog << " -d 0 --pid 1234 -i 50 -t 30\n"
              << std::endl;
}

static bool parse_args(int argc, char *argv[], tool_config &cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--list-gpus") {
            cfg.list_gpus = true;
        } else if (arg == "--list-counters") {
            cfg.list_counters = true;
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " requires an argument\n"; return false; }
            cfg.device_number = std::atoi(argv[++i]);
        } else if (arg == "-i" || arg == "--interval") {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " requires an argument\n"; return false; }
            cfg.interval_ms = std::atoi(argv[++i]);
            if (cfg.interval_ms <= 0) cfg.interval_ms = 100;
        } else if (arg == "-t" || arg == "--duration") {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " requires an argument\n"; return false; }
            cfg.duration_sec = std::atoi(argv[++i]);
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " requires an argument\n"; return false; }
            cfg.output_file = argv[++i];
        } else if (arg == "-p" || arg == "--process") {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " requires an argument\n"; return false; }
            cfg.process_name = argv[++i];
        } else if (arg == "--pid") {
            if (i + 1 >= argc) { std::cerr << "Error: " << arg << " requires an argument\n"; return false; }
            cfg.pid = std::atoi(argv[++i]);
        } else if (arg == "-v" || arg == "--verbose") {
            cfg.verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

static std::string format_timestamp(uint64_t ns) {
    time_t secs = static_cast<time_t>(ns / 1000000000ULL);
    uint64_t nsec_part = ns % 1000000000ULL;
    struct tm tm_buf = {};
    localtime_r(&secs, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    char result[80];
    snprintf(result, sizeof(result), "%s.%09llu", buf, (unsigned long long)nsec_part);
    return std::string(result);
}

static std::string sample_value_str(const hwcpipe::counter_sample &sample) {
    char buf[64];
    switch (sample.type) {
    case hwcpipe::counter_sample::type::uint64:
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)sample.value.uint64);
        break;
    case hwcpipe::counter_sample::type::float64:
        snprintf(buf, sizeof(buf), "%.6f", sample.value.float64);
        break;
    default:
        snprintf(buf, sizeof(buf), "N/A");
        break;
    }
    return std::string(buf);
}

struct counter_entry {
    hwcpipe_counter counter;
    std::string name;
    std::string units;
};

int main(int argc, char *argv[]) {
    tool_config cfg;
    if (!parse_args(argc, argv, cfg)) {
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    platform_info plat = detect_platform();
    bool libmali_loaded = try_preload_libmali(plat);

    print_platform_info(plat, std::cout);

    if (cfg.list_gpus) {
        std::cout << "============================================================\n"
                  << " GPU Device Scan\n"
                  << "============================================================\n";
        bool found = false;
        for (const auto &gpu : hwcpipe::find_gpus()) {
            print_gpu_info(gpu, std::cout);
            found = true;
        }
        if (!found) {
            std::cout << "    No Arm Mali GPU devices found.\n";
            if (!libmali_loaded) {
                std::cout << "    Hint: libmali.so not found. Try running with root or\n"
                          << "          set HWCPIPE_BACKEND_INTERFACE environment variable.\n";
            }
        }
        return 0;
    }

    hwcpipe::gpu gpu(cfg.device_number);
    if (!gpu) {
        TOOL_LOGE("Failed to initialize GPU device %d", cfg.device_number);
        TOOL_LOGE("Possible causes:");
        TOOL_LOGE("  - No Arm Mali GPU present on this device");
        TOOL_LOGE("  - No permission to access /dev/mali%d", cfg.device_number);
        TOOL_LOGE("  - libmali.so not found or incompatible");
        TOOL_LOGE("Suggested fixes:");
        TOOL_LOGE("  - Run as root: sudo %s", argv[0]);
        TOOL_LOGE("  - On Android: adb root && adb shell %s", argv[0]);
        TOOL_LOGE("  - Check device node: ls -la /dev/mali*");
        return 1;
    }

    print_gpu_info(gpu, std::cout);

    if (cfg.list_counters) {
        list_all_counters(gpu, std::cout);
        return 0;
    }

    hwcpipe::counter_database db;
    hwcpipe::counter_metadata meta;
    std::vector<counter_entry> counter_entries;

    for (hwcpipe_counter counter : db.counters_for_gpu(gpu)) {
        auto ec = db.describe_counter(counter, meta);
        if (!ec) {
            counter_entries.push_back({counter, meta.name, meta.units ? meta.units : ""});
        }
    }

    if (counter_entries.empty()) {
        TOOL_LOGE("No counters available for this GPU");
        return 1;
    }

    std::cout << "============================================================\n"
              << " Configuring Sampler\n"
              << "============================================================\n"
              << "    Counters:     " << counter_entries.size() << "\n"
              << "    Interval:     " << cfg.interval_ms << " ms\n"
              << "    Duration:     " << (cfg.duration_sec > 0 ? std::to_string(cfg.duration_sec) + "s" : "until stopped") << "\n";
    if (!cfg.output_file.empty()) {
        std::cout << "    Output:       " << cfg.output_file << "\n";
    }
    if (!cfg.process_name.empty()) {
        std::cout << "    Process:      " << cfg.process_name << "\n";
    }
    if (cfg.pid > 0) {
        std::cout << "    PID:          " << cfg.pid << "\n";
    }
    std::cout << std::endl;

    hwcpipe::sampler_config config(gpu);
    int added = 0;
    for (const auto &entry : counter_entries) {
        auto ec = config.add_counter(entry.counter);
        if (ec) {
            if (cfg.verbose) {
                TOOL_LOGE("Failed to add counter %s: %s", entry.name.c_str(), ec.message().c_str());
            }
        } else {
            ++added;
        }
    }
    std::cout << "    Added:        " << added << "/" << counter_entries.size() << " counters\n" << std::endl;

    if (added == 0) {
        TOOL_LOGE("No counters could be added to the sampler");
        return 1;
    }

    auto sampler = hwcpipe::sampler<>(config);
    if (!sampler) {
        TOOL_LOGE("Failed to create sampler - backend initialization failed");
        TOOL_LOGE("This typically means:");
        TOOL_LOGE("  - The kernel driver does not support hardware counter sampling");
        TOOL_LOGE("  - Another process is already using the counters");
        TOOL_LOGE("  - The driver version is incompatible with this library");
        TOOL_LOGE("Try setting HWCPIPE_BACKEND_INTERFACE=vinstr or kinstr_prfcnt");
        return 1;
    }

    auto ec = sampler.start_sampling();
    if (ec) {
        TOOL_LOGE("Failed to start sampling: %s", ec.message().c_str());
        return 1;
    }

    std::ofstream csv_file;
    if (!cfg.output_file.empty()) {
        csv_file.open(cfg.output_file, std::ios::out | std::ios::trunc);
        if (!csv_file.is_open()) {
            TOOL_LOGE("Failed to open output file: %s", cfg.output_file.c_str());
            auto ec_stop = sampler.stop_sampling();
            (void)ec_stop;
            return 1;
        }
        csv_file << "timestamp_ns";
        for (const auto &entry : counter_entries) {
            csv_file << "," << entry.name;
        }
        csv_file << "\n";
    }

    std::cout << "============================================================\n"
              << " Sampling Started\n"
              << "============================================================\n" << std::endl;

    auto start_time = std::chrono::steady_clock::now();
    int sample_count = 0;
    bool process_monitoring = !cfg.process_name.empty() || cfg.pid > 0;
    bool target_was_alive = true;
    bool target_disappeared = false;

    while (g_running) {
        if (cfg.duration_sec > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= cfg.duration_sec) {
                std::cout << "\n    Duration limit reached (" << cfg.duration_sec << "s)\n";
                break;
            }
        }

        if (process_monitoring) {
            bool alive = false;
            if (cfg.pid > 0) {
                alive = process_exists_by_pid(cfg.pid);
            } else if (!cfg.process_name.empty()) {
                alive = process_exists_by_name(cfg.process_name);
            }

            if (!alive) {
                if (target_was_alive) {
                    if (cfg.verbose) {
                        std::cout << "    Target process no longer running, finishing capture...\n";
                    }
                    target_was_alive = false;
                    target_disappeared = true;
                    break;
                }
            } else {
                target_was_alive = true;
            }
        }

        usleep(static_cast<useconds_t>(cfg.interval_ms) * 1000);

        ec = sampler.sample_now();
        if (ec) {
            if (cfg.verbose) {
                TOOL_LOGE("Sample failed: %s", ec.message().c_str());
            }
            continue;
        }

        ++sample_count;

        if (cfg.verbose && sample_count % 10 == 1) {
            std::cout << "    Sample #" << sample_count << ":\n";
        }

        if (csv_file.is_open()) {
            hwcpipe::counter_sample sample;
            bool first = true;
            uint64_t ts = 0;

            for (const auto &entry : counter_entries) {
                ec = sampler.get_counter_value(entry.counter, sample);
                if (!ec) {
                    if (first) {
                        csv_file << sample.timestamp;
                        ts = sample.timestamp;
                        first = false;
                    }
                    csv_file << "," << sample_value_str(sample);
                } else {
                    if (first) {
                        csv_file << "0";
                        first = false;
                    }
                    csv_file << ",";
                }
            }
            csv_file << "\n";
            csv_file.flush();
        }

        if (cfg.verbose && sample_count % 10 == 1) {
            hwcpipe::counter_sample sample;
            for (size_t i = 0; i < counter_entries.size() && i < 10; ++i) {
                const auto &entry = counter_entries[i];
                ec = sampler.get_counter_value(entry.counter, sample);
                if (!ec) {
                    std::cout << "        " << entry.name << " = " << sample_value_str(sample);
                    if (!entry.units.empty()) {
                        std::cout << " " << entry.units;
                    }
                    std::cout << "\n";
                }
            }
            if (counter_entries.size() > 10) {
                std::cout << "        ... (" << counter_entries.size() - 10 << " more counters)\n";
            }
        }
    }

    ec = sampler.stop_sampling();
    if (ec) {
        TOOL_LOGE("Failed to stop sampling: %s", ec.message().c_str());
    }

    if (csv_file.is_open()) {
        csv_file.close();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "\n============================================================\n"
              << " Sampling Complete\n"
              << "============================================================\n"
              << "    Samples collected: " << sample_count << "\n"
              << "    Total duration:    " << total_ms / 1000.0 << " seconds\n"
              << "    Counters per sample: " << counter_entries.size() << "\n";
    if (target_disappeared) {
        if (cfg.pid > 0) {
            std::cout << "    Stopped reason:    PID " << cfg.pid << " exited\n";
        } else {
            std::cout << "    Stopped reason:    Process '" << cfg.process_name << "' exited\n";
        }
    }
    if (!cfg.output_file.empty()) {
        std::cout << "    Output file:       " << cfg.output_file << "\n";
    }
    std::cout << std::endl;

    return 0;
}
