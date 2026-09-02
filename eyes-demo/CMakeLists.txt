cmake_minimum_required(VERSION 3.16)

# Only build `main` and what it transitively requires. This keeps Wi-Fi, BT,
# NVS, and every other unused ESP-IDF component out of the binary.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eyes_demo)
