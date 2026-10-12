<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Building a forked log pipeline

This guide shows how to build a logging system shaped like a fork:

```
 callers (threads, IRQs)
        |
   extended_logger        one shared write point
        |
  fanout_log_sink         serializes writers, applies back-pressure
   /     |      \
 pin0   pin1   pin2       each pin owns its storage and its own lock
  |      |       |
 reader reader reader     each reader pulls from "its" pin independently
```

Every record is written once at the shared point and lands in every pin's
storage, in the same order. Each pin is drained by its own consumer (UART
pump, flash writer, network exporter, ...) at its own speed.

Headers: `log/extended_msg.hpp`, `log/extended_sink.hpp`,
`log/extended_logger.hpp`, `log/sync.hpp`.

## 1. Concepts

| Piece | Role |
|---|---|
| `extended_log_msg` | `log_msg` plus `log_meta` (pid, tid, cpu, flags). |
| `log_flag::can_wait` | Set by the metadata callback when the calling context may sleep. Clear in IRQ/NMI/scheduler code. |
| `extended_log_sink` | Type-erased handle to a "pin". Optional `full()` lets it report back-pressure. |
| `fanout_log_sink` | Holds up to N pins. One write point; orders thread writers FIFO. |
| `Channel` | The lock plus sleep/wake primitive blocked writers wait on. |
| `space_notifier` | Callback a pin's consumer calls after freeing space. |

No component busy-waits: thread writers sleep on the channel, and priority
(IRQ) writers never wait at all.

## 2. Writer classes and locking

```
IRQ writer     (can_wait clear): pins directly                      -- no fan-out lock, never sleeps
thread writer  (can_wait set):   channel lock: take ticket, sleep until my turn
                                 [if a pin is full: sleep up to the timeout]
                                 release channel lock, write all pins, hand over the turn
reader:        pin's own lock -> release -> channel lock -> wake_all
```

* **IRQ writers have ultimate priority.** They never take a ticket and never
  touch the channel lock, so no sleeping thread writer can delay them. They
  write straight to every pin; a full pin overwrites its oldest data (or
  drops). The pin's own lock must therefore be IRQ-safe and short.
* **Thread writers are FIFO.** Each takes a ticket under the channel lock and
  sleeps on the channel until it is served, so records from concurrent
  threads reach every pin in arrival order and all pins see the same order
  (IRQ records are interleaved wherever they land).
* **The turn holder waits for space.** If a pin is full it sleeps up to
  `set_wait_timeout_us()` (still holding the turn, so later writers are not
  allowed to overtake it), then writes or discards the record (counted in
  `dropped()`). Waiting for the turn itself has no deadline, so the worst
  case for one caller is roughly (writers ahead of it) x timeout.
* **Readers never wait for writers.** They take only the pin's lock, then
  wake the channel. A pin refilled by an IRQ writer after the turn holder saw
  space simply drops that thread record (its `can_wait` is set).
* **Lock order:** channel lock, then pin lock. Call `notify_space()` after
  releasing the pin lock, from thread context (it takes the channel lock; for
  IRQ-driven draining, defer it to a thread or task queue).

### What happens on a full pin

| Context | Pin full | Result |
|---|---|---|
| `can_wait` clear (IRQ) | yes | Written through. The pin overwrites its oldest data (or drops). Never sleeps. |
| `can_wait` set (thread) | no | Written, in FIFO order. |
| `can_wait` set (thread) | yes | Sleeps up to `set_wait_timeout_us()`; then written, or discarded and counted in `dropped()`. |

The decision is made per record from the metadata, not from a fixed sink
setting.

## 3. Ordering

Ordering is among thread writers sharing one fan-out, by ticket (arrival at
the channel lock), not by the record's `time` field. For strict ordering
guarantees the `Channel`'s lock must be a sleeping lock that cannot be taken
from IRQ context (`mtx(9)` default, `reloco::mutex`); that is also why IRQ
writers skip it.

## 4. Step by step

### 4a. Write a pin (storage + traits)

A pin is any type with its own storage and lock, plus traits.

```cpp
#include <microfmt/log/extended_sink.hpp>

// Fixed-capacity record store. It owns its lock so the reader (any thread)
// and the single fan-out writer can touch it safely.
class record_store {
public:
  // Writer side (fan-out thread writers one at a time; IRQ writers concurrently,
  // so this lock must be short and IRQ-safe).
  void push(const microfmt::log::extended_log_msg &msg) noexcept {
    lock_.lock();
    if (full_unlocked()) {
      // IRQ context: make room by dropping the oldest record.
      // Thread context: the fan-out already waited; if still full, drop.
      if (msg.meta.can_wait()) { lock_.unlock(); return; }
      drop_oldest_unlocked();
    }
    append_unlocked(msg);
    lock_.unlock();
  }

  // Backs full(): "would this record not fit right now?".
  bool would_overflow(const microfmt::log::extended_log_msg &msg) noexcept;

  // Reader side: takes ONLY this pin's lock.
  bool pop(record &out) noexcept;

private:
  reloco::spin_lock lock_;     // the pin's own lock
  // ... storage (reloco::array, ring buffer, ...)
};

struct record_store_tag {};     // selects the traits below

template <> struct microfmt::log::extended_log_sink_traits<record_store_tag> {
  using context_type = record_store;   // state the handle points at

  // Mandatory: store the record.
  static void log(microfmt::value_ref<record_store> ctx, const extended_log_msg &msg) noexcept {
    ctx->push(msg);
  }
  // Optional: report back-pressure. Omit it and the pin is never "full".
  static bool full(microfmt::value_ref<record_store> ctx, const extended_log_msg &msg) noexcept {
    return ctx->would_overflow(msg);
  }
};
```

Rules for `log`: honor `msg.meta.can_wait()` as above, never block, never
allocate. Rules for `full`: cheap, no side effects, only takes the pin's own
lock.

### 4b. Build the fork

```cpp
#include <microfmt/log/extended_logger.hpp>

record_store uart_store, flash_store;

// 2 pins; thread writers queue FIFO and sleep on a condition variable.
microfmt::log::fanout_log_sink<2, microfmt::log::condition_variable_wait_channel> fan;

fan.add_sink(microfmt::log::extended_log_sink(record_store_tag{}, uart_store));
fan.add_sink(microfmt::log::extended_log_sink(record_store_tag{}, flash_store));
fan.set_wait_timeout_us(5000);   // a thread waits at most 5 ms per record
```

Stores and the fan-out must outlive every handle built from them.

### 4c. The shared write point and context metadata

```cpp
// Describes the *calling* context. Runs on every record: keep it cheap and
// context-safe.
microfmt::log::log_meta describe_context(void *, const microfmt::log::log_msg &) noexcept {
  microfmt::log::log_meta m;
  m.pid = current_pid();
  m.tid = current_tid();
  m.cpu = current_cpu();
  m.set(microfmt::log::log_flag::can_wait, !in_interrupt());  // the key decision
  return m;
}

microfmt::log::extended_logger<> logger("kern", fan.as_sink());
logger.set_meta_callback(&describe_context);

logger.info("boot {}", 1);   // same formatting API as the plain logger
```

### 4d. Readers (one per pin)

```cpp
void uart_pump() {                    // runs in its own thread/task
  record r;
  while (uart_store.pop(r)) {         // takes only the pin's lock
    uart_write(r);
  }
  fan.notify_space();                 // pin lock already released; wakes waiters
}
```

Or hand `fan.notifier()` (a `space_notifier`) to the pin's reader so it can
call `notify()` itself without knowing the fan-out type. Call it after every
drain that freed space; spurious calls are harmless.

## 5. Porting the wait channel

A `Channel` pairs the outer lock with the sleep/wake primitive:

```cpp
struct my_channel {
  void lock() noexcept;
  void unlock() noexcept;
  template <typename Pred> bool wait_for(std::uint64_t timeout_us, Pred pred) noexcept;
  void wake_all() noexcept;
};
```

`timeout_us == microfmt::log::wait_forever` means no deadline (used while
waiting for a FIFO turn).

`wait_for` is called with the lock held, sleeps releasing it, re-evaluates
`pred()` under the lock, and returns the final `pred()`.

FreeBSD kernel sketch (not compiled here):

```cpp
class bsd_channel {
public:
  bsd_channel() noexcept { mtx_init(&mtx_, "logwait", nullptr, MTX_DEF); cv_init(&cv_, "logwait"); }
  void lock() noexcept   { mtx_lock(&mtx_); }
  void unlock() noexcept { mtx_unlock(&mtx_); }
  template <typename Pred> bool wait_for(std::uint64_t us, Pred pred) noexcept {
    const int ticks = us == microfmt::log::wait_forever ? 0 : static_cast<int>(us * hz / 1'000'000) + 1;
    while (!pred()) {
      if (ticks == 0) {
        cv_wait(&cv_, &mtx_);            // no deadline
      } else if (cv_timedwait(&cv_, &mtx_, ticks) == EWOULDBLOCK) {
        return pred();
      }
    }
    return true;
  }
  void wake_all() noexcept { cv_broadcast(&cv_); }
private:
  struct mtx mtx_;
  struct cv cv_;
};
```

Use `null_wait_channel` when writers must never sleep (discard on full).

## 6. Checklist

* Every pin has its **own** lock; the fan-out never touches pin internals.
* Pin locks are short and IRQ-safe (IRQ writers lock them directly).
* The metadata callback sets `can_wait` correctly; IRQ code must clear it.
* Pins overwrite or drop when `can_wait` is clear; they never sleep.
* Readers take only the pin lock, then call `notify_space()` *after*
  releasing it.
* Watch `fan.dropped()` to size pins and the timeout.
* A nested `fanout_log_sink` is itself a valid pin (it reports `full`).
* For ready-to-copy sink templates (ring, console, filter, batch) see
  [Log sink templates](log-sinks-guide.md).
