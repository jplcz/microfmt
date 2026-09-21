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

> **VS Code snippets:** the templates in section 3 (`3a`/`3b`/`3c`) are also
> available as editor snippets — `mf-provider-specialize`,
> `mf-provider-vtable`, `mf-provider-stateless` — generated into
> `.vscode/microfmt.code-snippets` by
> `tools/snippets/generate-vscode-snippets.py` from the plain-text templates
> under `tools/snippets/templates/`. That file is gitignored like the rest of
> `.vscode/`; run the script locally (or after editing
> `tools/snippets/manifest.json`/`templates/*.tmpl`) to (re)generate it.

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
    microfmt::format_to(out, "your_type{{...}}");
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
#include <microfmt/reloco.hpp>

class your_type_view {
public:
  // `value_ref<const your_type>` rejects rvalue/temporary bindings, so a
  // dangling `source_` is a compile error instead of a runtime hazard. See
  // [Lifetime safety](lifetime-safety.md) for the full rationale.
  constexpr your_type_view(microfmt::value_ref<const your_type> source,
                           microfmt::span<char> scratch) noexcept
      : source_(source), scratch_(scratch) {}

  void render(const microfmt::sink &out) const noexcept {
    // Use *source_ (or source_->...) plus scratch_ for any decoding or
    // traversal storage; write incrementally.
  }

private:
  microfmt::value_ref<const your_type> source_;
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
microfmt::format_to(out, "{}",
                    your_type_view{microfmt::value_ref<const your_type>(value),
                                  microfmt::span<char>{scratch}});
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
microfmt::format_to(out, "value={}\n", 42);
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

  [[nodiscard]] microfmt::sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
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
microfmt::format_to(my_sink.as_sink(), "id={}\n", 7);
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

There are two different tasks this pattern covers, with two different
templates below:

* **3a. Plug into an existing provider abstraction** — specialize an
  *already-defined* `*_traits<Tag>` (e.g. `address_space_traits`,
  `register_context_traits`) for your own backend. This is by far the more
  common case and requires no vtable code at all; the library already
  defines the `*_ref` type and its vtable.
* **3b. Define a brand-new provider abstraction** — you are adding a new
  kind of pluggable customization point to your own code (not one of the
  library's existing ones), so you need the tag, the traits primary
  template, the type-erased `*_ref` class, *and* its vtable derivation.
  Use this template when 3a does not apply because no matching `*_traits`
  exists yet.

### 3a. Specialize an existing provider

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

### 3b. Define a new provider abstraction (full vtable derivation)

This is the complete, self-contained template for creating a new
`*_ref`-style type-erased handle from scratch: a tag, a traits primary
template, a two-word handle (context pointer + vtable pointer) with a
mandatory operation and an optional operation, and an owning wrapper. Every
`*_ref` type in the inspector (`address_space_ref`, `register_context_ref`,
`unwind_hint_registry_ref`, ...) is built from this same skeleton — copy it,
rename the placeholders, and add/remove operations as needed.

```cpp
#include <microfmt/reloco.hpp> // RELOCO_LIFETIMEBOUND, value_ref
#include <type_traits>
#include <utility>

// 1. An empty tag identifies a concrete implementation.
struct your_provider_tag {};

// 2. Primary template, intentionally left undefined. Each backend provides
//    a specialization (see step 6) with a `context_type` and static
//    operations; instantiating the primary template is a compile error,
//    which is the desired "you forgot to specialize this" diagnostic.
template <typename Tag> struct your_provider_traits;

namespace detail {

// 3. Compile-time detection for an *optional* trait operation ("write").
//    Required operations (like "read") need no detection: just call them
//    directly from the vtable trampoline in step 4 and let a missing
//    specialization fail to compile with a clear error.
template <typename Tag, typename = void>
struct has_your_provider_write : std::false_type {};

template <typename Tag>
struct has_your_provider_write<
    Tag, std::void_t<decltype(your_provider_traits<Tag>::write(
             std::declval<microfmt::value_ref<
                 typename your_provider_traits<Tag>::context_type>>(),
             std::declval<int>(), std::declval<int>()))>> : std::true_type {};

} // namespace detail

// 4. The type-erased handle: one context pointer plus one vtable pointer.
//    No virtual base class, no RTTI, no allocation -- `s_vtbl<Tag>` below
//    is a distinct static object per Tag, and its address acts as a
//    lightweight, per-Tag "type id" the handle carries around.
class your_provider_ref {
public:
  struct vtable {
    bool (*read)(const void *ctx, int key, int &out_value) noexcept;
    // `write` is nullptr for backends whose traits omit it (read-only).
    bool (*write)(void *ctx, int key, int value) noexcept;
  };

  constexpr your_provider_ref() noexcept = default;

  // Binds the handle to a caller-owned context. `Context` must be (or
  // derive from) the Tag's declared `context_type`.
  template <typename Tag, typename Context,
            typename Traits = your_provider_traits<Tag>,
            std::enable_if_t<std::is_convertible_v<
                                 Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr your_provider_ref(Tag, Context &ctx RELOCO_LIFETIMEBOUND) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  [[nodiscard]] bool read(int key, int &out_value) const noexcept {
    return vtbl_ && vtbl_->read(ctx_, key, out_value);
  }

  // Optional operations are exposed via a `can_*`/operation pair so callers
  // can distinguish "unsupported" from "failed this time".
  [[nodiscard]] bool can_write() const noexcept { return vtbl_ && vtbl_->write; }

  bool write(int key, int value) const noexcept {
    return vtbl_ && vtbl_->write &&
           vtbl_->write(const_cast<void *>(ctx_), key, value);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  // Trampolines recover the concrete `context_type` from the erased `void*`
  // and forward to the Traits specialization selected by `Tag`. One
  // trampoline instantiation exists per Tag the handle is ever bound to.
  template <typename Tag>
  static bool read_entry(const void *ctx, int key, int &out_value) noexcept {
    using context_type = typename your_provider_traits<Tag>::context_type;
    const auto &typed = *static_cast<const context_type *>(ctx);
    return your_provider_traits<Tag>::read(
        microfmt::value_ref<const context_type>(typed), key, out_value);
  }

  // Optional-operation trampolines are themselves selected at compile time:
  // `if constexpr` picks between a real trampoline and a null function
  // pointer, so unsupported operations cost nothing and are detectable via
  // `vtbl_->write == nullptr` (exposed above as `can_write()`).
  template <typename Tag>
  static constexpr auto write_entry() noexcept {
    using context_type = typename your_provider_traits<Tag>::context_type;
    if constexpr (detail::has_your_provider_write<Tag>::value) {
      return +[](void *ctx, int key, int value) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        return your_provider_traits<Tag>::write(
            microfmt::value_ref<context_type>(typed), key, value);
      };
    } else {
      return static_cast<bool (*)(void *, int, int) noexcept>(nullptr);
    }
  }

  // One `constexpr` vtable instance per Tag, built once at compile time and
  // stored in `.rodata` -- this *is* the "vtable derivation": deriving a
  // concrete function-pointer table from whatever `Tag`'s Traits
  // specialization provides, with no runtime registration step.
  template <typename Tag>
  static constexpr vtable s_vtbl{&read_entry<Tag>, write_entry<Tag>()};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// 5. Optional owning wrapper: holds `context_type` by value so the context
//    and the handle share a single object's lifetime.
template <typename Tag> class your_provider {
public:
  using traits_type = your_provider_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit your_provider(context_type context) noexcept
      : context_(std::move(context)) {}

  // Ref-qualified `&`: calling `.ref()` on a temporary owning wrapper is a
  // compile error, since the returned handle would otherwise outlive the
  // `context_` it points into.
  [[nodiscard]] constexpr your_provider_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return your_provider_ref(Tag{}, context_);
  }

private:
  context_type context_;
};

// 6. A concrete backend: specialize the traits primary template from step 2.
struct your_provider_context {
  int storage[16]{};
};

template <> struct your_provider_traits<your_provider_tag> {
  using context_type = your_provider_context;

  static bool read(microfmt::value_ref<const context_type> ctx, int key,
                    int &out_value) noexcept {
    if (key < 0 || key >= 16)
      return false;
    out_value = ctx->storage[key];
    return true;
  }

  // Omit entirely (do not stub it out) for a read-only backend; `can_write()`
  // will then report `false` for handles bound to this Tag.
  static bool write(microfmt::value_ref<context_type> ctx, int key,
                    int value) noexcept {
    if (key < 0 || key >= 16)
      return false;
    ctx->storage[key] = value;
    return true;
  }
};

// --- Usage ---
your_provider_context state{};
your_provider_ref ref{your_provider_tag{}, state};
int value = 0;
if (ref.read(3, value)) { /* ... */ }
if (ref.can_write())
  ref.write(3, 99);

your_provider<your_provider_tag> owned{your_provider_context{}};
auto owned_ref = owned.ref();
```

Adapting this template:

* Add one `<operation>_entry`/`vtable` field pair per operation; keep
  mandatory operations un-detected (a missing specialization should fail to
  compile) and gate every optional operation behind a `has_your_provider_*`
  trait-detection struct, following the `write` example.
* If some backends have no per-instance data, give them `context_type = void`
  traits instead of an empty struct — see 3c below for the ref-side changes
  this requires.
* Keep every vtable function pointer `noexcept`; the handle's own public
  methods should be `noexcept` as well so failures are reported through
  return values, not exceptions.

### 3c. Stateless traits (`context_type = void`)

Some backends have no per-instance data at all — every call is answered
purely from global/static state (the current process's own address space,
a fixed hardware register bank, a compile-time-known symbol table, ...).
For these, declare `using context_type = void;` and drop the
`value_ref<...>` parameter from every operation entirely, rather than
specializing traits with an empty placeholder struct.

```cpp
// A stateless backend: no context object, so every operation is a plain
// static function with no context parameter at all.
struct your_stateless_provider_tag {};

template <> struct your_provider_traits<your_stateless_provider_tag> {
  using context_type = void;

  static bool read(int key, int &out_value) noexcept {
    // ... answer purely from global/static state, no `ctx` parameter ...
    out_value = key * 2;
    return true;
  }
};
```

The `*_ref` handle from 3b needs two changes to support both stateful and
stateless tags side by side: a second constructor overload taking only the
tag (no context argument), enabled via `std::is_void_v<...>`; and an
`if constexpr` branch inside each entry trampoline that skips the
`value_ref` wrapping entirely for stateless tags. Both branches populate the
same `s_vtbl<Tag>`, so callers use `read()`/`write()` identically regardless
of which kind of tag they were bound to:

```cpp
class your_provider_ref {
public:
  struct vtable {
    bool (*read)(const void *ctx, int key, int &out_value) noexcept;
  };

  constexpr your_provider_ref() noexcept = default;

  // Stateless tag: no context object required.
  template <typename Tag, typename Traits = your_provider_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit your_provider_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  // Stateful tag: bind to a caller-owned context (same as 3b).
  template <typename Tag, typename Context,
            typename Traits = your_provider_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<
                                     Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr your_provider_ref(Tag, Context &ctx RELOCO_LIFETIMEBOUND) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  [[nodiscard]] bool read(int key, int &out_value) const noexcept {
    return vtbl_ && vtbl_->read(ctx_, key, out_value);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static bool read_entry(const void *ctx, int key, int &out_value) noexcept {
    using context_type = typename your_provider_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      // Stateless: `ctx` is always nullptr here, ignore it and call the
      // traits operation directly.
      (void)ctx;
      return your_provider_traits<Tag>::read(key, out_value);
    } else {
      const auto &typed = *static_cast<const context_type *>(ctx);
      return your_provider_traits<Tag>::read(
          microfmt::value_ref<const context_type>(typed), key, out_value);
    }
  }

  template <typename Tag>
  static constexpr vtable s_vtbl{&read_entry<Tag>};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// --- Usage: no context object anywhere. ---
your_provider_ref stateless_ref{your_stateless_provider_tag{}};
int value = 0;
if (stateless_ref.read(21, value)) { /* value == 42 */ }
```

Notes:

* An *optional* stateless operation needs its own SFINAE-detection struct,
  separate from the stateful one, since its expected signature has no
  `value_ref<...>` parameter to detect against (mirror
  `detail::has_stateless_address_space_write_bytes` alongside
  `detail::has_address_space_write_bytes` in
  `include/microfmt/inspector/address_space.hpp`).
* Do not default-construct an empty `context_type` struct just to keep a
  single code path; `void` is the correct signal both to the compiler (no
  storage, no pointer dereference) and to a reader of the traits
  specialization (this backend genuinely has no per-instance state).
* A tag's traits must pick one shape per operation (stateless or stateful,
  not both); the two constructor overloads above are already mutually
  exclusive on `std::is_void_v<typename Traits::context_type>`, so a given
  `Tag` can only ever match one of them.
