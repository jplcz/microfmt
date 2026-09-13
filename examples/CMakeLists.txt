function(add_microfmt_example NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE microfmt::microfmt)
    target_compile_features(${NAME} PRIVATE cxx_std_20)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_microfmt_example(demo_hexdump hexdump_demo.cpp)
add_microfmt_example(ansi_demo ansi_demo.cpp)
