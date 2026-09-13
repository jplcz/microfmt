
if(Boost_FOUND)
    add_definitions(-DMICROFMT_ENABLE_BOOST_UUID=1)
endif()

function(add_microfmt_example NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE microfmt::microfmt)
    target_compile_features(${NAME} PRIVATE cxx_std_20)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(Boost_FOUND)
        target_link_libraries(${NAME} PRIVATE Boost::boost)
    endif()
endfunction()

add_microfmt_example(demo_hexdump hexdump_demo.cpp)
add_microfmt_example(ansi_demo ansi_demo.cpp)
add_microfmt_example(markdown_demo markdown_demo.cpp)
add_microfmt_example(bitfield_demo bitfield_demo.cpp)
add_microfmt_example(fixedpoint_demo fixedpoint_demo.cpp)
add_microfmt_example(semver_demo semver_demo.cpp)
add_microfmt_example(units_demo units_demo.cpp)
add_microfmt_example(chrono_demo chrono_demo.cpp)
add_microfmt_example(ranges_demo ranges_demo.cpp)
add_microfmt_example(escaped_demo escaped_demo.cpp)
add_microfmt_example(fmt_demo fmt_demo.cpp)
add_microfmt_example(fmt_bridge_demo fmt_bridge_demo.cpp)
add_microfmt_example(stdio_demo stdio_demo.cpp)
add_microfmt_example(binary_demo binary_demo.cpp)
add_microfmt_example(base_views_demo base_views_demo.cpp)
add_microfmt_example(math_demo math_demo.cpp)
add_microfmt_example(monad_demo monad_demo.cpp)
add_microfmt_example(source_location_demo source_location_demo.cpp)
add_microfmt_example(ring_buffer_demo ring_buffer_demo.cpp)
add_microfmt_example(tee_sink_demo tee_sink_demo.cpp)
add_microfmt_example(syslog_demo syslog_demo.cpp)
add_microfmt_example(container_sink_demo container_sink_demo.cpp)

if(MICROFMT_COMPILER_SUPPORTS_CXX23)
    set_target_properties(monad_demo PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endif()

if(Boost_FOUND)
    add_microfmt_example(uuid_demo uuid_demo.cpp)

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
    message(STATUS "microfmt: Boost found - built UUID and Describe demos")
else()
    message(STATUS "microfmt: Boost >= 1.77 not found - skipping UUID and Describe demos")
endif()
