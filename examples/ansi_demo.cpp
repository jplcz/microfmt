#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/ansi.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

static void terminal_write(void * /*ctx*/, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

int main() {
  microfmt::sink term{nullptr, terminal_write};

  // ------------------------------------------------------------------------
  // 1. Full Color Palette (Standard & Bright)
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 1. Standard & Bright Colors ===\n");
  microfmt::format_to(
      term,
      "  Standard: {}Black{} {}Red{} {}Green{} {}Yellow{} {}Blue{} {}Magenta{} "
      "{}Cyan{} {}White{}\n"
      "  Bright:   {}Gray{} {}B-Red{} {}B-Green{} {}B-Yellow{} {}B-Blue{} "
      "{}B-Magenta{} {}B-Cyan{} {}B-White{}\n\n",
      microfmt::ansi::fg_black, microfmt::ansi::reset, microfmt::ansi::fg_red,
      microfmt::ansi::reset, microfmt::ansi::fg_green, microfmt::ansi::reset,
      microfmt::ansi::fg_yellow, microfmt::ansi::reset, microfmt::ansi::fg_blue,
      microfmt::ansi::reset, microfmt::ansi::fg_magenta, microfmt::ansi::reset,
      microfmt::ansi::fg_cyan, microfmt::ansi::reset, microfmt::ansi::fg_white,
      microfmt::ansi::reset,

      microfmt::ansi::fg_gray, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_red, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_green, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_yellow, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_blue, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_magenta, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_cyan, microfmt::ansi::reset,
      microfmt::ansi::fg_bright_white, microfmt::ansi::reset);

  // ------------------------------------------------------------------------
  // 2. Scoped Auto-Reset Value Wrappers
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 2. Scoped Value Wrappers ===\n");
  microfmt::format_to(
      term, "  Result: {}, Warning: {}, Value: {}, Debug: {}\n\n",
      microfmt::ansi::green("SUCCESS"), microfmt::ansi::yellow("LOW_MEMORY"),
      microfmt::ansi::cyan(1337), microfmt::ansi::gray("0x20000140"));

  // ------------------------------------------------------------------------
  // 3. Status Banners with Backgrounds & Modifiers
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 3. Badges and Status Banners ===\n");
  microfmt::format_to(
      term,
      "  {}{}{} FATAL {} Core dump written to flash @ {}\n"
      "  {}{}{} WARN  {} Sensor #3 reading out of threshold ({})\n"
      "  {}{}{} INFO  {} Peripheral clock sync locked at {}\n\n",
      microfmt::ansi::bg_bright_red, microfmt::ansi::bold,
      microfmt::ansi::fg_white, microfmt::ansi::reset,
      microfmt::ansi::cyan("0x08040000"), microfmt::ansi::bg_yellow,
      microfmt::ansi::bold, microfmt::ansi::fg_black, microfmt::ansi::reset,
      microfmt::ansi::yellow("89.4 C"), microfmt::ansi::bg_blue,
      microfmt::ansi::bold, microfmt::ansi::fg_bright_white,
      microfmt::ansi::reset, microfmt::ansi::green("480 MHz"));

  // ------------------------------------------------------------------------
  // 4. Telemetry Log Table
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 4. Embedded Telemetry Table ===\n");
  microfmt::format_to(
      term,
      "  {}[CH]{}  {}[PARAMETER]{}     {}[VALUE]{}      {}[STATUS]{}\n"
      "  -----------------------------------------------\n",
      microfmt::ansi::bold, microfmt::ansi::reset, microfmt::ansi::bold,
      microfmt::ansi::reset, microfmt::ansi::bold, microfmt::ansi::reset,
      microfmt::ansi::bold, microfmt::ansi::reset);

  struct Metric {
    uint8_t id;
    const char *name;
    int32_t val;
    const char *unit;
    enum { OK, WARN, ERR } status;
  };

  const Metric metrics[] = {{1, "Core Temp", 42, "C", Metric::OK},
                            {2, "VCC Core", 1180, "mV", Metric::OK},
                            {3, "Rail 3V3", 3080, "mV", Metric::WARN},
                            {4, "Bus Faults", 14, "cnt", Metric::ERR}};

  for (const auto &m : metrics) {
    if (m.status == Metric::OK) {
      microfmt::format_to(
          term, "   {:02d}   {:<12} {:>6} {:<4}  {}\n", m.id, m.name, m.val,
          m.unit, microfmt::ansi::styled("[  OK  ]", microfmt::ansi::ok_style));
    } else if (m.status == Metric::WARN) {
      microfmt::format_to(
          term, "   {:02d}   {:<12} {:>6} {:<4}  {}\n", m.id, m.name, m.val,
          m.unit,
          microfmt::ansi::styled("[ WARN ]", microfmt::ansi::warn_style));
    } else {
      microfmt::format_to(
          term, "   {:02d}   {:<12} {:>6} {:<4}  {}\n", m.id, m.name, m.val,
          m.unit,
          microfmt::ansi::styled("[ FAIL ]", microfmt::ansi::error_style));
    }
  }

  microfmt::format_to(term, "\n");
  return 0;
}