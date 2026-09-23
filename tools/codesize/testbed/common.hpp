// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// Shared "application" types and the single output sink used by every
// generated modules/module_NNN.cpp. See README.md for the overall design:
// this testbed measures microfmt's (and reloco's, via microfmt's formatter
// specializations for reloco containers/wrappers) code size in a realistic
// multi-file application, not microfmt against unrelated libraries.
//
// Defining TESTBED_STATIC_STRINGS switches every generated call site's
// format string from a runtime microfmt::string_view to
// MICROFMT_STRING(...) (compile-time, unrolled) -- the one axis of
// microfmt's own API this testbed deliberately varies, since docs/usage.md
// and README.md call out that trade-off as code-size relevant.

#include <microfmt/formatters/bitfield.hpp>
#include <microfmt/formatters/error.hpp>
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/formatters/monad.hpp>
#include <microfmt/formatters/pointer.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <microfmt/formatters/reloco.hpp>
#include <microfmt/formatters/semver.hpp>
#include <microfmt/formatters/string.hpp>
#include <microfmt/formatters/styled.hpp>
#include <microfmt/formatters/uuid.hpp>
#include <microfmt/microfmt.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(TESTBED_STATIC_STRINGS)
#define TB_FMT(s) MICROFMT_STRING(s)
#else
#define TB_FMT(s) microfmt::string_view(s, sizeof(s) - 1)
#endif

namespace testbed {

// ============================================================================
// Application-defined type, with its own microfmt::formatter specialization
// -- the "user type" every real integration has at least a few of.
// ============================================================================

struct point3 {
  int x;
  int y;
  int z;
};

using out_t = const microfmt::sink &;

// Implemented out-of-line in sink.cpp so every call below is an opaque
// function call the optimizer cannot see through or fold away, the way a
// real logging/UART transport would be.
microfmt::sink &backend_sink() noexcept;

[[nodiscard]] inline out_t backend_out() noexcept { return backend_sink(); }

} // namespace testbed

template <> struct microfmt::formatter<testbed::point3> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const testbed::point3 &p, const microfmt::sink &out) const noexcept {
    microfmt::format_to(out, "{{{}, {}, {}}}", p.x, p.y, p.z);
  }
};

// No-op unless RELOCO_SHARED/MICROFMT_SHARED are both defined (see run.sh's
// "multiso-shared" scenario); everywhere else this include has zero effect.
// Included down here (rather than up top with the rest) since it needs
// testbed::point3 and its formatter<> specialization already defined --
// see shared_type_instances.hpp's own doc comment for why this is
// application code, not testbed-framework code.
#include "shared_type_instances.hpp"
