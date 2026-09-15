# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: MIT

add_executable(bench_stack_painting bench_stack_painting.cpp)
target_link_libraries(bench_stack_painting PRIVATE microfmt)
microfmt_enable_warnings(bench_stack_painting)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(bench_stack_painting PRIVATE
    -O2                          # Benchmark against realistic release optimizations
    -fno-omit-frame-pointer      # Keep clean frame tracking
  )

#   if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
#     target_compile_options(bench_stack_painting PRIVATE -fstack-usage)
#   elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#     target_compile_options(bench_stack_painting PRIVATE -fstack-size-section)
#   endif()
endif()

include(CTest)
add_test(NAME bench_stack_painting COMMAND bench_stack_painting)

# Custom target: 'cmake --build <build_dir> --target run_stack_bench'
add_custom_target(run_stack_bench
  COMMAND bench_stack_painting
  DEPENDS bench_stack_painting
  COMMENT "Running microfmt stack high-water mark benchmark..."
  USES_TERMINAL
)