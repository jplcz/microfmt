// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause
//
// Builds the actual MICROFMT_SHARED_BUILD/RELOCO_SHARED_BUILD shared
// library hosting both libraries' "heavy", *_SHARED-eligible symbols:
//
//  - microfmt: vformat_to, detail::int_formatter_specs::parse,
//    detail::int_formatter_specs::format_int_impl<int64_t/uint64_t> (see
//    microfmt/detail/compat.hpp and microfmt/microfmt.ipp), and
//    format_hexdump plus every other migrated formatters/*.ipp (see
//    microfmt/microfmt_compile.hpp's own doc comment for the full list).
//
//  - reloco: its mutex backend, default/heap/stack allocators, and its own
//    char-based string aliases (reloco::basic_string/basic_sso_string/
//    basic_string_view<char> -- reloco::sso_string et al. are typedefs
//    over these) (see reloco/reloco_compile.hpp).
//
//  - shared_type_instances.hpp: the testbed's own application-level
//    MICROFMT_FORMATTER_INSTANCE/RELOCO_TYPE_INSTANCE list, for the
//    specific reloco container/wrapper instantiations (and formatter<T>
//    specializations over them) that generate_modules.py's SHAPE_CATALOG
//    actually uses -- neither library can pre-migrate these itself, since
//    both are templates over a testbed-supplied T; see that header's own
//    doc comment.
//
// -- that every shard_*.so / main_multiso translation unit would otherwise
// each independently instantiate and duplicate (see run.sh's "multiso"
// mode and README.md).
//
// Every other translation unit in the "multiso-shared" scenario instead
// defines MICROFMT_SHARED and RELOCO_SHARED (consume-only, without either
// _BUILD macro) and links against the .so built from this one file.
// Nothing here is testbed-specific: this is exactly what a real
// application would build once and ship alongside its plugins/feature
// .so's -- microfmt_compile.hpp/reloco_compile.hpp are the umbrella
// headers meant for exactly this one translation unit; see their own doc
// comments for why individual shard/consumer TUs never include them
// themselves.
//
// reloco_compile.hpp is included first so that RELOCO_SHARED/
// RELOCO_SHARED_BUILD are already in their final state by the time
// microfmt_compile.hpp transitively (re-)includes the same reloco headers
// (include guards mean each header's behavior is fixed by the macro state
// at its *first* inclusion) -- see docs/shared-library.md.
#define RELOCO_SHARED
#define RELOCO_SHARED_BUILD
#include <reloco/reloco_compile.hpp>

#define MICROFMT_SHARED
#define MICROFMT_SHARED_BUILD
#include <microfmt/microfmt_compile.hpp>

// common.hpp itself #includes shared_type_instances.hpp (after defining
// testbed::point3, which reloco::expected<testbed::point3, ...> needs) --
// see common.hpp's own doc comment for why it's included down there rather
// than at the top.
#include "common.hpp"

