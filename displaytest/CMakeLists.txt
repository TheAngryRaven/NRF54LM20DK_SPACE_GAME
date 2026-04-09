#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(displaytest)

target_sources(app PRIVATE
    src/main.c
    src/render.c
    src/game.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
