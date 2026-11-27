# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

cmake_minimum_required(VERSION 3.16)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(LYRA_ARM_CXX_EXE arm-none-eabi-g++)
find_program(LYRA_ARM_CC_EXE arm-none-eabi-gcc)
find_program(LYRA_SCONS_EXE scons)

if(LYRA_ARM_CXX_EXE AND LYRA_ARM_CC_EXE)
    add_custom_target(oyk_lyra_arm_verify
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/verify_arm_build.py"
                --cxx "${LYRA_ARM_CXX_EXE}"
                --cc "${LYRA_ARM_CC_EXE}"
                --root "${CMAKE_SOURCE_DIR}"
        COMMENT "Cross-compiling Luckfox Lyra (ARMv7 Cortex-A7 + RT-Thread stubs) and checking <= 1024 B stack"
    )
else()
    message(STATUS "Skipping oyk_lyra_arm_verify (arm-none-eabi-g++ / arm-none-eabi-gcc not found)")
endif()

if(RK3506_BSP_ROOT AND EXISTS "${RK3506_BSP_ROOT}/SConstruct" AND LYRA_SCONS_EXE)
    add_custom_target(oyk_lyra_rtthread
        COMMAND ${LYRA_SCONS_EXE} -C "${RK3506_BSP_ROOT}" -j4
        COMMENT "Building full RT-Thread SMP firmware image in RK3506_BSP_ROOT"
    )
else()
    message(STATUS "Skipping oyk_lyra_rtthread (set -DRK3506_BSP_ROOT=... and install scons for full BSP image build)")
endif()
