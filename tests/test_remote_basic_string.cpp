// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/remote_basic_string.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace {

template <typename T> uintptr_t address_of(const T &object) noexcept {
  return reinterpret_cast<uintptr_t>(&object);
}

microfmt::address_space_ref local_space() {
  return microfmt::address_space_ref(microfmt::local_space_tag{});
}

struct pointer_string {
  uintptr_t data;
  size_t size;
};

struct size_selected_string {
  uintptr_t data;
  size_t size;
  char inline_data[8];
};

TEST(RemoteBasicString, RendersPointerAndSizeLayoutInChunks) {
  const char text[] = "remote string";
  const pointer_string value{address_of(text), sizeof(text) - 1};
  char scratch[4]{};
  const auto layout = microfmt::remote_basic_string_traits::
      pointer_size_layout<uintptr_t, size_t>(offsetof(pointer_string, data),
                                             offsetof(pointer_string, size));
  const auto view = microfmt::make_remote_basic_string_view(
      address_of(value), local_space(), scratch, layout);

  EXPECT_EQ(microfmt::format<32>("{}", view).view(), "remote string");
}

TEST(RemoteBasicString, PreservesEmbeddedNullCharacters) {
  const char text[] = {'a', '\0', 'b'};
  const pointer_string value{address_of(text), sizeof(text)};
  char scratch[2]{};
  const auto layout = microfmt::remote_basic_string_traits::
      pointer_size_layout<uintptr_t, size_t>(offsetof(pointer_string, data),
                                             offsetof(pointer_string, size));
  const auto view = microfmt::make_remote_basic_string_view(
      address_of(value), local_space(), scratch, layout);
  const auto output = microfmt::format<8>("{}", view);

  EXPECT_EQ(output.view(), microfmt::string_view(text, sizeof(text)));
}

TEST(RemoteBasicString, SelectsInlineStorageAndTruncates) {
  size_selected_string value{};
  value.size = 6;
  value.inline_data[0] = 'i';
  value.inline_data[1] = 'n';
  value.inline_data[2] = 'l';
  value.inline_data[3] = 'i';
  value.inline_data[4] = 'n';
  value.inline_data[5] = 'e';

  char scratch[3]{};
  const auto layout = microfmt::remote_basic_string_traits::
      size_selected_layout<uintptr_t, size_t>(
          offsetof(size_selected_string, data),
          offsetof(size_selected_string, size),
          offsetof(size_selected_string, inline_data), 7);
  const auto view = microfmt::make_remote_basic_string_view(
      address_of(value), local_space(), scratch, layout, 4);

  EXPECT_EQ(microfmt::format<16>("{}", view).view(), "inli...");
}

TEST(RemoteBasicString, SelectsHeapStorageAndReportsFaults) {
  const char text[] = "heap backed";
  size_selected_string value{address_of(text), sizeof(text) - 1, {}};
  char scratch[5]{};
  const auto layout = microfmt::remote_basic_string_traits::
      size_selected_layout<uintptr_t, size_t>(
          offsetof(size_selected_string, data),
          offsetof(size_selected_string, size),
          offsetof(size_selected_string, inline_data), 7);
  const auto view = microfmt::make_remote_basic_string_view(
      address_of(value), local_space(), scratch, layout);
  EXPECT_EQ(microfmt::format<32>("{}", view).view(), "heap backed");

  value.data = 0;
  EXPECT_EQ(microfmt::format<32>("{}", view).view(),
            microfmt::format<32>("<fault@{:#x}>", address_of(value)).view());
}

TEST(RemoteBasicString, SupportsExplicitLayoutCallbacks) {
  struct callback_state {
    ptrdiff_t data_offset;
    ptrdiff_t size_offset;
  };

  const char text[] = "callback";
  const pointer_string value{address_of(text), sizeof(text) - 1};
  char scratch[3]{};
  const callback_state state{offsetof(pointer_string, data),
                             offsetof(pointer_string, size)};
  const auto layout = microfmt::remote_basic_string_traits::callback_layout(
      [state](microfmt::address_space_ref space, uintptr_t object_address,
              size_t &size) noexcept {
        return static_cast<bool>(
            space.read(object_address +
                           static_cast<uintptr_t>(state.size_offset),
                       size));
      },
      [state](microfmt::address_space_ref space, uintptr_t object_address,
              uintptr_t &data_address, size_t) noexcept {
        return static_cast<bool>(
            space.read(object_address +
                           static_cast<uintptr_t>(state.data_offset),
                       data_address));
      });
  const auto view = microfmt::make_remote_basic_string_view(
      address_of(value), local_space(), scratch, layout);

  EXPECT_EQ(microfmt::format<16>("{}", view).view(), "callback");
}

} // namespace
