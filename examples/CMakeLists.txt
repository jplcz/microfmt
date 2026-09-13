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
add_microfmt_example(can_demo can_demo.cpp)
add_microfmt_example(cbor_demo cbor_demo.cpp)
add_microfmt_example(fixedpoint_demo fixedpoint_demo.cpp)
add_microfmt_example(filter_view_demo filter_view_demo.cpp)
add_microfmt_example(i2c_demo i2c_demo.cpp)
add_microfmt_example(json_demo json_demo.cpp)
add_microfmt_example(semver_demo semver_demo.cpp)
add_microfmt_example(spi_demo spi_demo.cpp)
add_microfmt_example(units_demo units_demo.cpp)
add_microfmt_example(chrono_demo chrono_demo.cpp)
add_microfmt_example(ranges_demo ranges_demo.cpp)
add_microfmt_example(escaped_demo escaped_demo.cpp)
add_microfmt_example(fmt_demo fmt_demo.cpp)
add_microfmt_example(fmt_bridge_demo fmt_bridge_demo.cpp)
add_microfmt_example(styled_demo styled_demo.cpp)
add_microfmt_example(styled_sink_demo styled_sink_demo.cpp)
add_microfmt_example(stdio_demo stdio_demo.cpp)
add_microfmt_example(binary_demo binary_demo.cpp)
add_microfmt_example(base_views_demo base_views_demo.cpp)
add_microfmt_example(math_demo math_demo.cpp)
add_microfmt_example(map_view_demo map_view_demo.cpp)
add_microfmt_example(monad_demo monad_demo.cpp)
add_microfmt_example(source_location_demo source_location_demo.cpp)
add_microfmt_example(ring_buffer_demo ring_buffer_demo.cpp)
add_microfmt_example(tee_sink_demo tee_sink_demo.cpp)
add_microfmt_example(tuple_demo tuple_demo.cpp)
add_microfmt_example(syslog_demo syslog_demo.cpp)
add_microfmt_example(container_sink_demo container_sink_demo.cpp)
add_microfmt_example(pmr_sink_demo pmr_sink_demo.cpp)
add_microfmt_example(pointer_demo pointer_demo.cpp)
add_microfmt_example(android_log_demo android_log_demo.cpp)
add_microfmt_example(logger_demo logger_demo.cpp)
add_microfmt_example(default_logger_demo default_logger_demo.cpp)
add_microfmt_example(register_demo register_demo.cpp)
add_microfmt_example(grid_view_demo grid_view_demo.cpp)
add_microfmt_example(bintime_demo bintime_demo.cpp)

if(TARGET PkgConfig::MICROFMT_SYSTEMD)
  add_microfmt_example(systemd_demo systemd_demo.cpp)
  target_link_libraries(systemd_demo PRIVATE PkgConfig::MICROFMT_SYSTEMD)
else()
  message(STATUS "microfmt: libsystemd not found - skipping systemd demo")
endif()

if(TARGET PkgConfig::MICROFMT_TIZEN_DLOG)
  add_microfmt_example(tizen_dlog_demo tizen_dlog_demo.cpp)
  target_link_libraries(tizen_dlog_demo PRIVATE PkgConfig::MICROFMT_TIZEN_DLOG)
else()
  message(STATUS "microfmt: Tizen dlog not found - skipping Tizen dlog demo")
endif()

if(MICROFMT_COMPILER_SUPPORTS_CXX23)
  set_target_properties(
    monad_demo
    PROPERTIES CXX_STANDARD 23
               CXX_STANDARD_REQUIRED ON
               CXX_EXTENSIONS OFF)
endif()

if(Boost_FOUND)
  add_microfmt_example(uuid_demo uuid_demo.cpp)

  add_executable(demo_boost_describe describe_demo.cpp)
  target_link_libraries(demo_boost_describe PRIVATE microfmt::microfmt
                                                    Boost::boost)
  target_compile_features(demo_boost_describe PRIVATE cxx_std_20)
  set_target_properties(
    demo_boost_describe
    PROPERTIES FOLDER "Examples" RUNTIME_OUTPUT_DIRECTORY
                                 "${CMAKE_BINARY_DIR}/examples")
  message(STATUS "microfmt: Boost found - built UUID and Describe demos")
else()
  message(
    STATUS
      "microfmt: Boost >= 1.77 not found - skipping UUID and Describe demos")
endif()
