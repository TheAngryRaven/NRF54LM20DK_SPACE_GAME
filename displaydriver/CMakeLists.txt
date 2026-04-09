cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(displaydriver)

target_sources(app PRIVATE
    src/main.c
    src/st7365.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
