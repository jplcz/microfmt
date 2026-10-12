<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Writing log sinks: templates for common use cases

Ready-to-copy advanced sinks ("pins") for the forked pipeline described in
[Forked log pipeline](log-fanout.md). Each template is a plain class (state
plus its own lock) and a tag with `extended_log_sink_traits`. All templates
below compile cleanly under the project's strict warning set
(`-Wunsafe-buffer-usage`, `-Wconversion`, ...).

## 0. The contract every sink follows

| Rule | Why |
|---|---|
| `log(ctx, msg)` never blocks on anything but the sink's own short lock, never allocates | IRQ writers call it directly, with no queue in front |
| When full and `msg.meta.can_wait()` is **clear** (IRQ): overwrite the oldest data, or drop | IRQ writers have priority and cannot sleep |
| When full and `msg.meta.can_wait()` is **set** (thread): drop (the fan-out already waited) | The only way to get here is a race with an IRQ writer |
| `full(ctx, msg)` is cheap, side-effect free, takes only the sink's own lock | Called by the fan-out while it holds its channel lock |
| The sink's lock must be **IRQ-safe** if IRQ writers use the sink | On one CPU, an IRQ preempting a holder of a plain spin lock deadlocks; use a lock that masks interrupts (e.g. `spinlock_enter()`-style on FreeBSD) |
| Readers take only the sink's lock, release it, **then** wake writers | Lock order is channel lock, then sink lock |
| `full` omitted means "never full": always accepts | Right for sinks that are always fast (console) |

Pick a template by use case:

| Use case | Template | Reports full? |
|---|---|---|
| Buffer in RAM, drained by another thread | A: ring pin | yes |
| Write immediately to a kernel console/UART | B: console pin | no |
| Route only some records to another sink | C: filter pin | forwards |
| Batch for slow media (flash, network) | D: batch pin | yes |

Common includes and namespace for every template:

```cpp
#include <microfmt/log/extended_logger.hpp>   // also pulls in extended_sink.hpp
#include <reloco/spin_lock.hpp>               // or your IRQ-safe lock

namespace tpl {
using namespace microfmt::log;
// ... templates A-D go here ...
} // namespace tpl
```

## A. Ring pin: RAM buffer with a separate reader

Use when a producer-side context (any thread or IRQ) must hand records to a
slower consumer (a UART pump, a log file thread). Fixed-size slots keep
`push` allocation-free and bounded.

```cpp
struct slot {
  level lvl{level::info};
  std::uint32_t pid{0}, tid{0}, cpu{0};
  std::uint16_t len{0};
  reloco::array<char, 120> text{};
};

template <std::size_t Slots, typename Lock> class ring_pin {
public:
  void push(const extended_log_msg &msg) noexcept {
    lock_.lock();
    if (count_ == Slots) {
      if (msg.meta.can_wait()) { // a thread writer that lost a race: drop
        ++dropped_;
        lock_.unlock();
        return;
      }
      head_ = (head_ + 1) % Slots; // IRQ writer: overwrite the oldest
      --count_;
      ++overwritten_;
    }
    slot &s = slots_[(head_ + count_) % Slots];
    s.lvl = msg.lvl;
    s.pid = msg.meta.pid;
    s.tid = msg.meta.tid;
    s.cpu = msg.meta.cpu;
    const std::size_t n = msg.payload.size() < s.text.size() ? msg.payload.size() : s.text.size();
    for (std::size_t i = 0; i < n; ++i) {
      s.text[i] = msg.payload[i];
    }
    s.len = static_cast<std::uint16_t>(n);
    ++count_;
    lock_.unlock();
  }

  [[nodiscard]] bool is_full() noexcept {
    lock_.lock();
    const bool r = count_ == Slots;
    lock_.unlock();
    return r;
  }

  [[nodiscard]] bool pop(slot &out) noexcept {
    lock_.lock();
    const bool ok = count_ != 0;
    if (ok) {
      out = slots_[head_];
      head_ = (head_ + 1) % Slots;
      --count_;
    }
    lock_.unlock();
    if (ok && notifier_) {
      notifier_.notify();
    }
    return ok;
  }

  void set_space_notifier(space_notifier n) noexcept { notifier_ = n; }

private:
  Lock lock_{};
  reloco::array<slot, Slots> slots_{};
  std::size_t head_{0}, count_{0}, dropped_{0}, overwritten_{0};
  space_notifier notifier_{};
};
template <std::size_t Slots, typename Lock> struct ring_pin_tag {};
```

Reader side (its own thread/task). `pop` takes only the pin's lock, then
calls the notifier you installed so waiting writers wake:

```cpp
ring.set_space_notifier(fan.notifier());   // once, at setup

tpl::slot rec;
while (ring.pop(rec)) {                    // takes only the pin's lock
  uart_write(rec.text.data(), rec.len);     // slow work happens with no lock held
}
```

Tuning: size `Slots` for the longest burst an IRQ can produce between reader
wake-ups; watch `overwritten_`/`dropped_` (expose them as accessors if you
need them).

## B. Console pin: always accepts, no `full`

Use for a fast, synchronous kernel console: a polled UART, an early
framebuffer, or FreeBSD's `cnputs(9)`. The pin does not talk to an OS stream.
It is given the platform's raw "put string" primitive, so it works before the
OS is up and from IRQ context. Because it never reports full, it never causes
waiting. Pair it with a ring pin if the device is slow.

```cpp
// Platform primitive: writes bytes to the device. It must be IRQ-safe,
// non-blocking and must not allocate. Examples: a polled UART register
// loop, FreeBSD cnputs(9).
using console_put_fn = void (*)(void *device, std::string_view text) noexcept;

class console_pin {
public:
  console_pin(console_put_fn put, void *device) noexcept : put_(put), device_(device) {}

  void write(const extended_log_msg &msg) noexcept {
    // Format into a stack buffer first so each record reaches the device as
    // one contiguous line, even when CPUs or IRQs log concurrently.
    microfmt::buffer_sink<256> line;
    auto out = line.as_sink();
    microfmt::format_to(out, "[cpu{} tid{}] {}: {}\n", msg.meta.cpu, msg.meta.tid, to_short_string(msg.lvl), msg.payload);
    put_(device_, line.view());
  }

private:
  console_put_fn put_;
  void *device_;
};
struct console_pin_tag {};
```

If several CPUs share one device and the primitive is not itself atomic
per call, guard it with an IRQ-safe lock inside `put_`, not in `write`.

## C. Filter pin: route by metadata

Wraps another pin and forwards only matching records. User flag bits
(`log_meta::set_user_flags`) are a cheap way to tag records (for example
bit 0 = "audit", bit 1 = "security"). It must forward `full` and `flush`,
otherwise back-pressure from the inner pin would be hidden.

```cpp
class filter_pin {
public:
  filter_pin(extended_log_sink inner, std::uint32_t required_user_flags) noexcept
      : inner_(inner), required_(required_user_flags) {}

  void write(const extended_log_msg &msg) noexcept {
    if ((msg.meta.user_flags() & required_) == required_) {
      inner_.log(msg);
    }
  }
  [[nodiscard]] bool full(const extended_log_msg &msg) noexcept {
    return (msg.meta.user_flags() & required_) == required_ && inner_.full(msg);
  }
  void flush() noexcept { inner_.flush(); }

private:
  extended_log_sink inner_;
  std::uint32_t required_;
};
struct filter_pin_tag {};
```

## D. Batch pin: slow media

Use for flash or network exporters that prefer writing many records at once.
Records accumulate in RAM; `flush` (called by `logger.flush()`, or by your
timer) hands the batch to a drain callback. It reports full when the batch
is full, so thread writers wait (up to the fan-out timeout) while a flush is
in progress instead of losing records.

```cpp
template <std::size_t Batch, typename Lock> class batch_pin {
public:
  using drain_fn = void (*)(const slot *records, std::size_t count) noexcept;

  explicit batch_pin(drain_fn fn) noexcept : drain_(fn) {}

  void push(const extended_log_msg &msg) noexcept {
    lock_.lock();
    if (n_ < Batch) {
      slot &s = batch_[n_++];
      s.lvl = msg.lvl;
      s.tid = msg.meta.tid;
      const std::size_t len = msg.payload.size() < s.text.size() ? msg.payload.size() : s.text.size();
      for (std::size_t i = 0; i < len; ++i) {
        s.text[i] = msg.payload[i];
      }
      s.len = static_cast<std::uint16_t>(len);
    }
    lock_.unlock();
  }
  [[nodiscard]] bool is_full() noexcept {
    lock_.lock();
    const bool r = n_ == Batch;
    lock_.unlock();
    return r;
  }
  void flush() noexcept {
    lock_.lock();
    if (n_ != 0) {
      drain_(batch_.data(), n_);
      n_ = 0;
    }
    lock_.unlock();
  }

private:
  Lock lock_{};
  drain_fn drain_;
  reloco::array<slot, Batch> batch_{};
  std::size_t n_{0};
};
template <std::size_t Batch, typename Lock> struct batch_pin_tag {};
```

`flush` calls the drain callback under the pin's lock for simplicity. For
media that may take long, swap two buffers under the lock and write the
detached one outside it, so IRQ writers are never held up.

## E. Traits glue for all templates

```cpp
namespace microfmt::log {
template <std::size_t S, typename L> struct extended_log_sink_traits<tpl::ring_pin_tag<S, L>> {
  using context_type = tpl::ring_pin<S, L>;
  static void log(value_ref<context_type> c, const extended_log_msg &m) noexcept { c->push(m); }
  static bool full(value_ref<context_type> c, const extended_log_msg &) noexcept { return c->is_full(); }
};
template <> struct extended_log_sink_traits<tpl::console_pin_tag> {
  using context_type = tpl::console_pin;
  static void log(value_ref<context_type> c, const extended_log_msg &m) noexcept { c->write(m); }
};
template <> struct extended_log_sink_traits<tpl::filter_pin_tag> {
  using context_type = tpl::filter_pin;
  static void log(value_ref<context_type> c, const extended_log_msg &m) noexcept { c->write(m); }
  static bool full(value_ref<context_type> c, const extended_log_msg &m) noexcept { return c->full(m); }
  static void flush(value_ref<context_type> c) noexcept { c->flush(); }
};
template <std::size_t B, typename L> struct extended_log_sink_traits<tpl::batch_pin_tag<B, L>> {
  using context_type = tpl::batch_pin<B, L>;
  static void log(value_ref<context_type> c, const extended_log_msg &m) noexcept { c->push(m); }
  static bool full(value_ref<context_type> c, const extended_log_msg &) noexcept { return c->is_full(); }
  static void flush(value_ref<context_type> c) noexcept { c->flush(); }
};
} // namespace microfmt::log
```

## F. Putting it together

```cpp
tpl::ring_pin<8, reloco::spin_lock> ring;
tpl::console_pin con(&uart_put, &uart0);   // uart_put: your device primitive
tpl::batch_pin<4, reloco::mutex> batch(&drain);      // void drain(const tpl::slot *, std::size_t) noexcept
tpl::filter_pin audit(microfmt::log::extended_log_sink(
    tpl::batch_pin_tag<4, reloco::mutex>{}, batch), /*required user flags=*/0x1);

microfmt::log::fanout_log_sink<4, microfmt::log::condition_variable_wait_channel> fan;
fan.add_sink(microfmt::log::extended_log_sink(tpl::ring_pin_tag<8, reloco::spin_lock>{}, ring));
fan.add_sink(microfmt::log::extended_log_sink(tpl::console_pin_tag{}, con));
fan.add_sink(microfmt::log::extended_log_sink(tpl::filter_pin_tag{}, audit));
ring.set_space_notifier(fan.notifier());

microfmt::log::extended_logger<> logger("kern", fan.as_sink());
logger.set_meta_callback(&describe_context);   // sets pid/tid/cpu and log_flag::can_wait
logger.info("boot {}", 1);
```

All sinks and the fan-out must outlive every handle built from them.

## G. Testing a sink

* Fill it, then check `extended_log_sink::full(msg)` is `true`.
* Log with `can_wait` clear into a full sink: nothing blocks, oldest data is
  replaced (or the record dropped).
* Log with `can_wait` set into a full sink behind a fan-out: the caller waits,
  then proceeds after a reader pops and calls `notify_space()`.
* `tests/test_extended_log.cpp` has a minimal sink and these scenarios to copy.
