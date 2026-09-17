# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_microfmt_conan_consumer LANGUAGES CXX)

find_package(jplcz_microfmt CONFIG REQUIRED)

add_executable(package_consumer src/package_consumer.cpp)
target_link_libraries(package_consumer PRIVATE jplcz_microfmt::microfmt)
