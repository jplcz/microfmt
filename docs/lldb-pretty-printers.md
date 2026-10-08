# LLDB pretty printers

`microfmt` ships LLDB data formatters for the same sinks, buffers, and logger
covered by the [GDB pretty printers](gdb-pretty-printers.md), so `frame
variable`/`p` show `microfmt::buffer_sink written 2 of 64 bytes = "42"`
instead of raw `m_data`/`m_pos` fields. The summary strings match the GDB
printers.

Everything lives in `tools/lldb/microfmt_printers.py`.

## Loading

In a running session:

```
(lldb) command script import /path/to/microfmt/tools/lldb/microfmt_printers.py
```

Permanently, by adding the same line to `~/.lldbinit` (or to a project-local
`.lldbinit`, which LLDB only reads when `target.load-cwd-lldbinit` is on).

Importing the module runs `__lldb_init_module`, which registers every
formatter in the `microfmt` type category and enables it. To switch it off:
`type category disable microfmt`.

Unlike the GDB printers there is no embedded-in-the-binary mode: LLDB has no
portable equivalent of GDB's `.debug_gdb_scripts` section.

`microfmt::string_view`, `span`, `array`, `value_ptr`/`value_ref`,
`checked_value`, and `expected` are aliases of `reloco` types, so import
[reloco's LLDB printers](https://github.com/jplcz/reloco/blob/master/docs/lldb-pretty-printers.md)
as well to see them rendered.

## Coverage

Same set of types as the GDB printers (see
[Coverage](gdb-pretty-printers.md#coverage)): `span_sink`, `buffer_sink<N>`,
`c_string_sink<N>`, `c_string_span_sink`, `counting_sink`,
`iterator_sink<OutputIt>`, `memory_buffer<N>`, and `log::basic_logger`.
Type-erased sinks and argument lists are left as plain struct dumps for the
same reasons.

## Testing

`tools/lldb/testbed/run.sh` builds the shared
`tools/gdb/testbed/testbed.cpp` with Clang (`CXX`, default `clang++-24`;
reloco headers come from `../reloco/include` or
`MICROFMT_RELOCO_INCLUDE_DIR`), stops at the `GDB_BREAK` marker, runs `frame
variable` for every `// GDB_CHECK:` case under LLDB (`LLDB`, default
`lldb-24`), and verifies the expected substring. Adding a case to the GDB
testbed therefore covers both debuggers. It is not part of the main build or
CI; run it explicitly after editing `microfmt_printers.py`.
