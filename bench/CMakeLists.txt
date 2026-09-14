add_executable(bench_stack_painting bench_stack_painting.cpp)
target_link_libraries(bench_stack_painting PRIVATE microfmt)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(bench_stack_painting PRIVATE
    -O2                          # Benchmark against realistic release optimizations
    -fno-omit-frame-pointer      # Keep clean frame tracking
    -Wall
    -Wextra
  )

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(bench_stack_painting PRIVATE -fstack-usage)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(bench_stack_painting PRIVATE -fstack-size-section)
  endif()
endif()
