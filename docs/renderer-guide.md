<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Writing low-stack renderers

In `microfmt`, a `formatter<T>` is an adapter from a value to a `sink`. Keep
that adapter small. Any work that needs traversal state, decoding storage,
temporary text, or a substantial input object belongs in a dedicated view
class, not in the formatter's stack frame.

This separation is important for bare-metal code, ISRs, kernel-adjacent paths,
and deeply nested format calls. A formatter can be instantiated anywhere in a
formatting chain; large local arrays or copied structures in its `format`
method increase every caller's stack requirement.

## The renderer contract

Use three layers:

1. The application data owns its durable state.
2. A lightweight view references that data and caller-supplied working storage.
3. A thin `formatter<view>` delegates rendering to the view.

The view is the renderer. It owns the interpretation of the source data,
incremental traversal, bounds handling, and scratch-buffer use. The formatter
only parses an optional specifier and calls the view.

Do not make `formatter<heavy_type>` reconstruct a parser, collect a whole
range, or allocate a large temporary buffer. Wrap `heavy_type` in a view
instead.

## Avoid stack-heavy formatters

This formatter places a 256-byte buffer in every formatting call and mixes
rendering policy with the formatter adapter:

```cpp
template <> struct microfmt::formatter<packet> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const packet &value, const microfmt::sink &out) const noexcept {
    char decoded[256]; // Avoid: increases the formatter's stack frame.
    const size_t size = decode_packet(value, decoded, sizeof(decoded));
    microfmt::format_to(out, MICROFMT_STRING("{}"),
                        microfmt::string_view{decoded, size});
  }
};
```

Even if `decode_packet` is allocation-free, this pattern is costly when a
packet is rendered repeatedly or from a stack-limited execution context.

## Put the work in a view

Pass scratch storage to a view explicitly. The view stores only handles: a
reference or pointer to the source and a `microfmt::span<char>` over storage
owned by its caller.

```cpp
class packet_view {
public:
  constexpr packet_view(const packet &source,
                        microfmt::span<char> scratch) noexcept
      : source_(source), scratch_(scratch) {}

  void render(const microfmt::sink &out) const noexcept {
    const size_t decoded =
        decode_packet(source_, scratch_.data(), scratch_.size());
    const size_t size =
        decoded < scratch_.size() ? decoded : scratch_.size();
    microfmt::format_to(out, MICROFMT_STRING("{}"),
                        microfmt::string_view{scratch_.data(), size});
  }

private:
  const packet &source_;
  microfmt::span<char> scratch_;
};

template <> struct microfmt::formatter<packet_view> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const packet_view &view, const microfmt::sink &out) const noexcept {
    view.render(out);
  }
};
```

The view object itself is small enough to pass by value. The backing scratch
buffer can be a member of a long-lived device, logger, decoder, task context,
or other dedicated owner:

```cpp
class telemetry_renderer {
public:
  packet_view view(const packet &value) noexcept {
    return packet_view{value, microfmt::span<char>{scratch_, sizeof(scratch_)}};
  }

private:
  char scratch_[256]{};
};

telemetry_renderer renderer; // Storage is not allocated by the formatter.
microfmt::format_to(output, MICROFMT_STRING("{}"), renderer.view(current));
```

This makes scratch capacity a visible resource decision instead of an implicit
stack cost.

## Scratch-buffer rules

Follow these rules whenever a view receives scratch storage:

* Make the scratch buffer caller-owned. A view may reference it, but must not
  outlive it.
* Store scratch as `microfmt::span<char>` or `microfmt::span<std::byte>` so its
  size is always available to bounds checks.
* Format incrementally where possible. Do not first build an unbounded
  intermediate string.
* Define behavior for insufficient scratch explicitly: truncate safely, emit a
  diagnostic marker, or report failure through the view's documented contract.
* Do not share one mutable scratch buffer between concurrent render operations
  without synchronization. Give each task, core, logger, or request its own
  buffer when concurrent rendering is required.
* Do not retain pointers into scratch after `render` returns.

Remote inspector views follow this pattern: they carry an address-space handle
and a scratch span, then read and format one bounded piece at a time. This
keeps remote traversal state out of formatter-local arrays.

## Keep formatter parsing small

When a renderer supports specifiers, the formatter may store a few mode flags,
but it should still delegate data processing to the view:

```cpp
template <> struct microfmt::formatter<packet_view> {
  bool verbose{false};

  constexpr void parse(microfmt::format_parse_context &ctx) noexcept {
    verbose = !ctx.spec().empty() && ctx.spec().front() == '#';
  }

  void format(const packet_view &view, const microfmt::sink &out) const noexcept {
    view.render(out, verbose);
  }
};
```

Keep `parse` `constexpr` and `noexcept`. Store only the parsed configuration
needed for the current field; do not cache a decoded payload, a container of
elements, or a large text representation in the formatter.

## Choose the right view lifetime

Use a temporary view when it only contains references and spans:

```cpp
microfmt::format_to(out, MICROFMT_STRING("packet={}"),
                    packet_view{packet, scratch});
```

Use a persistent renderer object when it owns a dedicated work buffer or
configuration shared across calls. The persistent object can create temporary
views cheaply, as shown in the `telemetry_renderer` example. Never store a
view that refers to a short-lived local buffer.

For a renderer that must support nested formatting, provide separate scratch
regions to nested views. Reusing a single buffer while an inner formatting call
is still reading from it corrupts the outer view's transient data.

## Review checklist

Before adding a renderer, confirm that:

1. The formatter contains only small parsing state and no large local
   structures.
2. The view carries all source references and traversal state.
3. Temporary storage is explicit, bounded, and owned outside the formatter.
4. Every read and write respects the supplied scratch span's capacity.
5. Nested and concurrent use have a documented scratch-buffer strategy.
6. Literal internal format strings use `MICROFMT_STRING(...)`.
7. Tests cover normal output, constrained scratch capacity, and nested or
   repeated rendering when applicable.
