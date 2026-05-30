#
# Copyright (c) 2021-2025 Arm Limited.
#
# SPDX-License-Identifier: MIT
#

add_executable(api-example
    api_example.cpp
)

target_link_libraries(api-example
    PRIVATE hwcpipe
)

target_compile_options(api-example
    PRIVATE -Werror
            -Wswitch-default
            -Wswitch-enum
)

add_executable(gpu-counter-tool
    gpu_counter_tool.cpp
)

target_link_libraries(gpu-counter-tool
    PRIVATE hwcpipe
)

if(HWCPIPE_SYSCALL_LIBMALI)
    target_compile_definitions(gpu-counter-tool PRIVATE HWCPIPE_SYSCALL_LIBMALI=1)
endif()

target_compile_options(gpu-counter-tool
    PRIVATE -Werror
            -Wswitch-default
            -Wswitch-enum
)
