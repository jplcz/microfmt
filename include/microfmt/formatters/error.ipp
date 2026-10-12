// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// std-interop-file: out-of-line bodies formatting std::error_category/
// std::error_code -- the whole point of this file is std:: interop.
/** @file error.ipp @brief Out-of-line bodies for formatter<std::error_category>
 * / formatter<std::error_code> (see error.hpp). Included from error.hpp
 * itself, guarded on MICROFMT_SHARED_PROVIDE_DEFINITIONS (see
 * microfmt/detail/compat.hpp). Never included directly. */

// low-level formatting: raw character pointers and integer width conversions bounded by the buffer sizes checked here.
RELOCO_BEGIN_UNSAFE_CODE
MICROFMT_API void formatter<std::error_category>::format(const std::error_category &category,
                                                          const sink &out) const noexcept {
  const char *name = category.name();
  if (name != nullptr) {
    out.write(name);
  } else {
    out.write("unknown_category");
  }
}
RELOCO_END_UNSAFE_CODE

MICROFMT_API void formatter<std::error_code>::format(const std::error_code &ec, const sink &out) const noexcept {
  // Print the descriptive message if available, otherwise fallback to category:value
  const std::string msg = ec.message();
  if (!msg.empty()) {
    out.write(msg);
  }

  out.write(" [");

  // Format category name
  const formatter<std::error_category> cat_fmt = {};
  cat_fmt.format(ec.category(), out);

  out.write(":");

  // Format error integer value
  const formatter<int> int_fmt;
  int_fmt.format(ec.value(), out);

  out.write("]");
}
