// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/arm_exidx_unwinder.hpp>
#include <microfmt/inspector/chained_unwinder.hpp>
#include <microfmt/inspector/dwarf_decoder.hpp>
#include <microfmt/inspector/frame_pointer.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/inspector/unwind_hint.hpp>
#include <microfmt/sinks/stdio.hpp>

// The demo is unchanged except for the hint callback: callbacks receive the
// mutable register context so they can recover addresses held in GPRs.
