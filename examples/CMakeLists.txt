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
add_microfmt_example(markdown_demo markdown_demo.cpp)
add_microfmt_example(bitfield_demo bitfield_demo.cpp)
add_microfmt_example(fixedpoint_demo fixedpoint_demo.cpp)

find_package(Boost 1.77 QUIET)

if(Boost_FOUND)
    add_executable(demo_boost_describe describe_demo.cpp)
    target_link_libraries(demo_boost_describe PRIVATE 
        microfmt::microfmt 
        Boost::boost
    )
    target_compile_features(demo_boost_describe PRIVATE cxx_std_20)
    set_target_properties(demo_boost_describe PROPERTIES 
        FOLDER "Examples"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
    )
    message(STATUS "microfmt: Boost found - built demo_boost_describe")
else()
    message(STATUS "microfmt: Boost >= 1.77 not found - skipping demo_boost_describe")
endif()
