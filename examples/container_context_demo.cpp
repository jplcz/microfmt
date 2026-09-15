// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/remote_container.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte scratch[1024];

  // Define custom state for a mock sequence container (e.g., generating
  // values on the fly)
  struct SequenceState {
    int start_value;
    int count;
  };

  SequenceState my_sequence{10, 4}; // Generates 4 items starting from 10

  // Build the external context using the renamed make_container_context
  // helper
  auto context = microfmt::make_container_context(
      my_sequence,
      [](SequenceState &state, const microfmt::container_options &opts,
         microfmt::address_space_ref, microfmt::span<std::byte>,
         const microfmt::sink &out) noexcept {
        out.write(opts.open_bracket);

        for (int i = 0; i < state.count; ++i) {
          if (i > 0) {
            out.write(opts.entry_separator);
          }

          int current_item = state.start_value + (i * 10);

          // Respect container_options settings for keys and values
          if (opts.print_key) {
            microfmt::format_to(out, "k{}", i);
            out.write(opts.kv_separator);
          }

          if (opts.print_value) {
            microfmt::format_to(out, "{}", current_item);
          }
        }

        out.write(opts.close_bracket);
        return true;
      });

  // Configure formatting rules directly via container_options
  microfmt::container_options custom_opts{.print_key = true,
                                          .print_value = true,
                                          .kv_separator = " = ",
                                          .entry_separator = " | ",
                                          .open_bracket = "[ ",
                                          .close_bracket = " ]"};

  // Instantiate remote_container_view, passing the context pointer
  // (&context)
  uintptr_t mock_remote_address = 0x7FFF0000;
  microfmt::remote_container_view container_view(
      mock_remote_address, space_ref, scratch, &context, custom_opts);

  // Print the container view
  microfmt::print("Inspected Sequence: {}\n", container_view);
  // Output: Inspected Sequence: [ k0 = 10 | k1 = 20 | k2 = 30 | k3 = 40 ]

  return 0;
}