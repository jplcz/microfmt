<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Extending microfmt: formatters, sinks, and providers

microfmt has three independent extension points. Each is a small compile-time
customization point: no virtual inheritance, no allocation, and no runtime
registration.

| Extension point | Customizes | Deeper guide |
|---|---|---|
| `microfmt::formatter<T>` | How a value is rendered into a `sink` | [Writing low-stack renderers](renderer-guide.md) |
| `microfmt::sink` | Where formatted characters go | [Using microfmt](usage.md#stream-to-a-sink), [Bare-metal hardware sinks](bare-metal.md) |
| Tag + `*_traits<Tag>` + `context_type` | Pluggable inspector providers (address spaces, register contexts, unwinders, resolvers, containers, ...) | [Inspector: traits, contexts, and type erasure](inspector/traits-and-contexts.md) |

This page gives a minimal, fill-in-the-blank template for each. Copy the
template, replace the placeholder names, and consult the linked guide for
design rules (stack budget, lifetime, const/mutable access, testing
conventions) once the skeleton compiles.

## 1. Formatter template

A formatter is an adapter from a value to a `sink`. `parse` reads the
replacement-field specifier once (at compile time when the format string is a
`MICROFMT_STRING`); `format` writes the value's representation.

```cpp
#include <microfmt/microfmt.hpp>

struct your_type {
  // ... your fields ...
};

template <> struct microfmt::formatter<your_type> {
  // Optional: store parsed specifier flags here (keep this small).
  bool alternate{false};

  constexpr void parse(microfmt::format_parse_context &ctx) noexcept {
    alternate = !ctx.spec().empty() && ctx.spec().front() == '#';
  }

  void format(const your_type &value, const microfmt::sink &out) const noexcept {
    // Delegate to microfmt::format_to for nested fields; do not build an
    // intermediate std::string.
    microfmt::format_to(out, MICROFMT_STRING("your_type{{...}}"));
  }
};
```

Register it by including this specialization before any `format`/`format_to`
call that formats `your_type`.

**If rendering `your_type` needs more than a few local variables** (decoding
storage, a traversal cursor, a large temporary buffer): do not put that state
in the formatter. Wrap `your_type` in a small view class instead, and give
the view caller-owned scratch storage:

```cpp
class your_type_view {
public:
  constexpr your_type_view(const your_type &source,
                           microfmt::span<char> scratch) noexcept
      : source_(source), scratch_(scratch) {}

  void render(const microfmt::sink &out) const noexcept {
    // Use scratch_ for any decoding/traversal storage; write incrementally.
  }

private:
  const your_type &source_;
  microfmt::span<char> scratch_;
};

template <> struct microfmt::formatter<your_type_view> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const your_type_view &view, const microfmt::sink &out) const noexcept {
    view.render(out);
  }
};

// Usage: caller supplies and owns the scratch buffer.
char scratch[128];
microfmt::format_to(out, MICROFMT_STRING("{}"),
                    your_type_view{value, microfmt::span<char>{scratch}});
```

See [Writing low-stack renderers](renderer-guide.md) for the full rationale,
scratch-buffer rules, and a review checklist before adding a renderer.

## 2. Sink template

`microfmt::sink` is an opaque context pointer plus a `noexcept` write
callback (`void (*)(void *ctx, microfmt::string_view sv) noexcept`). No
inheritance is involved; any target that can accept a character slice can be
wrapped.

**Free-function form** (no persistent state beyond a single pointer):

```cpp
#include <microfmt/microfmt.hpp>

void your_write_fn(void *ctx, microfmt::string_view sv) noexcept {
  auto *target = static_cast<your_target_type *>(ctx);
  for (char c : sv) {
    your_target_putc(target, c); // your device/protocol write primitive
  }
}

your_target_type target{};
microfmt::sink out{&target, your_write_fn};
microfmt::format_to(out, MICROFMT_STRING("value={}\n"), 42);
```

**Class-adapter form** (preferred when the sink owns state, e.g. a buffer,
counters, or a hardware handle): expose an `as_sink()` method that returns a
`sink` bound to `this`, and make the class non-copyable/non-movable so that
bound pointer stays valid.

```cpp
#include <microfmt/microfmt.hpp>

class your_sink {
public:
  your_sink() noexcept = default;

  // Non-copyable/non-movable: as_sink() binds a raw `this` pointer.
  your_sink(const your_sink &) = delete;
  your_sink &operator=(const your_sink &) = delete;

  [[nodiscard]] microfmt::sink as_sink() noexcept MICROFMT_LIFETIMEBOUND {
    return microfmt::sink{
        this, [](void *ctx, microfmt::string_view sv) noexcept {
          static_cast<your_sink *>(ctx)->write(sv);
        }};
  }

  void write(microfmt::string_view sv) noexcept {
    for (char c : sv) {
      // ... append/transmit c ...
    }
  }

private:
  // ... your owned state (buffer, handle, counters, ...) ...
};

your_sink my_sink;
microfmt::format_to(my_sink.as_sink(), MICROFMT_STRING("id={}\n"), 7);
```

Rules for any sink implementation:

* `write_fn`/`write` must be `noexcept`; formatting never throws.
* Treat the character slice as a bounded chunk, not a complete message; a
  single `format_to` call may invoke the write callback many times.
* Do not allocate. Truncate, drop, or block per your target's documented
  policy when the underlying destination is full.
* If the sink is shared across concurrent writers, synchronize internally
  (see `ring_buffer_sink` for an atomics-based example) or document that
  callers must serialize access.

microfmt ships adapters for buffers, spans, output iterators, ring buffers,
counting, and several OS/hardware targets (`microfmt/sinks/`, `microfmt/hw/`,
`microfmt/log/sink.hpp`) — check whether an existing one already fits before
writing a new one. See [Using microfmt](usage.md#stream-to-a-sink) for the
core set and [Bare-metal hardware sinks](bare-metal.md) for PL011 UART and
ARM semihosting adapters.

## 3. Context_type-based provider template

Inspector-style providers (address spaces, register contexts, unwinders,
symbol resolvers, remote containers, unwind-hint registries, ...) use one
recurring pattern instead of virtual interfaces: an empty tag selects a
`*_traits<Tag>` specialization that declares a `context_type` and static
operations; a typed wrapper or caller-owned object holds the `context_type`
by value; and a type-erased `*_ref` borrows it for use in non-templated code.

```cpp
// 1. An empty tag identifies your implementation.
struct your_provider_tag {};

// 2. State your provider needs, held by value (no virtual base, no vtable).
struct your_provider_context {
  // ... whatever your operations need to read (and, if mutable, write) ...
};

// 3. Specialize the relevant `*_traits<Tag>` in namespace microfmt.
//    (Replace `some_provider_traits`/`operation` with the concrete inspector
//    trait/operation you are implementing -- see the table in
//    inspector/traits-and-contexts.md for the full list: address_space_traits,
//    register_context_traits, frame_unwinder_traits, symbol_resolver_traits,
//    unwind_hint_registry_traits, remote_forward_list_traits, and others.)
template <>
struct microfmt::some_provider_traits<your_provider_tag> {
  using context_type = your_provider_context;

  // Read-only operation: value_ref<const context_type>.
  static bool operation(microfmt::value_ref<const context_type> context,
                        /* ... operation-specific parameters ... */) noexcept {
    // ... implement using context->... ...
    return true;
  }
};

// 4a. Borrow a caller-owned context directly (useful when it is shared);
//     the `*_ref` type is constructed from the tag and a context reference:
your_provider_context state{/* ... */};
microfmt::some_provider_ref provider_ref{your_provider_tag{}, state};

// 4b. Or use a typed wrapper that owns context_type by value, when the
//     state naturally belongs to the provider itself, then call .ref():
microfmt::some_provider<your_provider_tag> owned{your_provider_context{/* ... */}};
auto ref_from_owned = owned.ref();
```

Notes:

* Use `microfmt::value_ref<const context_type>` for read-only operations and
  `microfmt::value_ref<context_type>` for operations that mutate state (e.g.
  register writes). Optional state in an already-erased handle uses
  `value_ptr<const T>`/`value_ptr<T>` instead.
* For a stateless provider, declare `using context_type = void;` and omit the
  context parameter from every operation.
* The context (or its owning wrapper) must outlive every `*_ref`/view derived
  from it — type erasure never transfers ownership.
* Optional operations on a trait are detected at compile time (SFINAE); omit
  an operation entirely rather than providing a stub that returns failure, so
  callers can distinguish "unsupported" from "failed this time".

See [Inspector: traits, contexts, and type erasure](inspector/traits-and-contexts.md)
for the full pattern rationale, three fully worked examples (a custom address
space, a custom remote forward list, and a custom unwind-hint registry),
ownership/lifetime rules, and the testing checklist every provider
specialization should satisfy.
