#include <microfmt/formatters/floating.hpp>
#include <microfmt/formatters/reloco.hpp>
#include <microfmt/gdb_printers.hpp>
#include <microfmt/log/logger.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/ring_buffer_sink_protocol_sink.hpp>
#include <microfmt/sinks/stdio.hpp>
#include <reloco/gdb_printers.hpp>

int main() {
  using namespace microfmt;
  using namespace reloco;

  // ========================================================================
  // The Shared Memory Boundary
  // ========================================================================
  // This could be an outline_ring_buffer mapped over an IPC / DMA region.
  // We use a small 256-byte inline buffer to intentionally force wrap-around
  // and demonstrate the flawless handling of `split_string_view`.
  inline_ring_buffer<char, 256> shared_ring;

  // ========================================================================
  // The Producer (e.g., Guest OS, WASM Module, Game Engine)
  // ========================================================================
  {
    log::ring_buffer_log_sink backend(shared_ring);
    log::logger sys_logger{"Engine", backend.as_sink()};

    println("--- Producer generating logs... ---");

    // These early messages will be cleanly EVICTED because we are about to
    // flood the 256-byte buffer.
    sys_logger.info(MICROFMT_STRING("Booting simulation..."));
    sys_logger.warn(MICROFMT_STRING("Initializing memory manager..."));
    sys_logger.error(MICROFMT_STRING("Failed to load config.json!"));

    // Flood the buffer to trigger the auto-eviction policy.
    int tick = 1;
    reloco::from_fn([&]() -> reloco::optional<int> {
      if (tick <= 15)
        return tick++;
      return reloco::nullopt;
    }).for_each([&](int i) {
      sys_logger.info(MICROFMT_STRING("Tick {:02} complete. CPU: {} ms"), i, 16.0 + (i * 0.1));
    });

    println("--- Producer finished. Buffer wrapped and old logs were evicted. ---\n");
  }

  // ========================================================================
  // The Consumer (e.g., Host OS, Telemetry Daemon, File Writer)
  // ========================================================================
  {
    log::ring_buffer_log_reader extractor(shared_ring);

    microfmt::println("--- Extracting & Rewriting Logs to JSON ---\n");

    std::size_t extracted_count =
        std::move(extractor)
            .map([](log::log_record_tx &tx) {
              // Formatting into a statically sized inline buffer
              auto json = microfmt::format<512>(MICROFMT_STRING("{{\"lvl\":{}, \"tag\":\"{}\", \"msg\":\"{}\"}}\n"),
                                                static_cast<int>(tx.get_level()), tx.logger_name(), tx.payload());

              // Commit the bytes to the ring buffer before the transaction ends
              tx.commit();

              // Pass the fully constructed inline buffer down the pipeline
              return json;
            })
            .fold(std::size_t{0}, [](std::size_t acc, const auto &json) {
              // Print the result and increment the accumulator
              println("{}", json.view());
              return acc + 1;
            });

    println("--- Dumped {} records (older records were successfully evicted) ---", extracted_count);
  }
  return 0;
}