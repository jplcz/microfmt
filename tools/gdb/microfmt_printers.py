# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""GDB pretty printers for the microfmt header-only library.

This module can be used in three interchangeable ways:

  1. Sourced directly in a running GDB session::

         (gdb) source /path/to/microfmt/tools/gdb/microfmt_printers.py

  2. Auto-loaded for a specific binary by placing a ``<binary>-gdb.py``
     next to it (or anywhere on GDB's auto-load path) containing::

         import sys
         sys.path.insert(0, "/path/to/microfmt/tools/gdb")
         import microfmt_printers
         microfmt_printers.register_microfmt_printers()

  3. Embedded directly into the binary's ``.debug_gdb_scripts`` section via
     ``include/microfmt/gdb_printers.hpp`` (see that header for details); GDB
     then loads and runs this exact source automatically when the binary is
     loaded, with no external file or path configuration required.

In all three cases the printers are registered against ``objfile`` (or the
global printer list, when there is no current object file) the moment this
module is loaded -- see the bottom of the file.

Type-erased sinks/views whose stored data can only be recovered by calling
through a function pointer or an externally-owned abstract interface (e.g.
``microfmt::sink``, ``microfmt::format_args``, ``microfmt::sinks::sbuf_sink``)
are intentionally not covered: invoking that callback from GDB's Python API
isn't reliably callable, and they add little value over default struct
printing.
"""

try:
    import gdb
    import gdb.printing
except ImportError:  # pragma: no cover - only importable inside GDB.
    gdb = None


def _char_pointer_string(ptr_val, length):
    """Renders a ``char*`` + length pair the way GDB renders C strings."""
    if length == 0:
        return '""'
    try:
        return ptr_val.lazy_string(length=int(length))
    except gdb.error:
        return "<unreadable>"


class MicrofmtSpanSinkPrinter:
    """Pretty printer for `microfmt::span_sink`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        buf = self.val["m_buf"]
        pos = int(self.val["m_pos"])
        cap = int(buf["m_size"])
        return "microfmt::span_sink written %d of %d bytes = %s" % (
            pos,
            cap,
            _char_pointer_string(buf["m_ptr"], pos),
        )


class MicrofmtBufferSinkPrinter:
    """Pretty printer for `microfmt::buffer_sink<N>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        capacity = int(self.val.type.template_argument(0))
        pos = int(self.val["m_pos"])
        return "microfmt::buffer_sink written %d of %d bytes = %s" % (
            pos,
            capacity,
            _char_pointer_string(self.val["m_data"], pos),
        )


class MicrofmtCStringSinkPrinter:
    """Pretty printer for `microfmt::c_string_sink<N>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        pos = int(self.val["m_pos"])
        max_payload = int(self.val["m_max_payload"])
        return "microfmt::c_string_sink written %d of %d bytes = %s" % (
            pos,
            max_payload,
            _char_pointer_string(self.val["m_data"], pos),
        )


class MicrofmtCStringSpanSinkPrinter:
    """Pretty printer for `microfmt::c_string_span_sink`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        data = self.val["m_data"]
        pos = int(self.val["m_pos"])
        max_payload = int(self.val["m_max_payload"])
        if int(data) == 0:
            return "microfmt::c_string_span_sink [discarding, no buffer]"
        return "microfmt::c_string_span_sink written %d of %d bytes = %s" % (
            pos,
            max_payload,
            _char_pointer_string(data, pos),
        )


class MicrofmtCountingSinkPrinter:
    """Pretty printer for `microfmt::counting_sink`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "microfmt::counting_sink counted %d byte(s)" % int(self.val["m_count"])


class MicrofmtIteratorSinkPrinter:
    """Pretty printer for `microfmt::iterator_sink<OutputIt>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "microfmt::iterator_sink at %s" % str(self.val["m_it"])


class MicrofmtMemoryBufferPrinter:
    """Pretty printer for `microfmt::memory_buffer<N>` (and its base class)."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["m_size"])
        capacity = int(self.val["m_capacity"])
        return "microfmt::memory_buffer of length %d, capacity %d = %s" % (
            size,
            capacity,
            _char_pointer_string(self.val["m_data"], size),
        )


class MicrofmtLoggerPrinter:
    """Pretty printer for `microfmt::log::basic_logger<MaxSinks, MsgBufferCapacity>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        name = self.val["name_"]
        level = self.val["level_"]
        sink_count = int(self.val["sink_count_"])
        return "microfmt::log::basic_logger %s level=%s sinks=%d" % (
            _char_pointer_string(name["data_"], name["size_"]),
            str(level),
            sink_count,
        )


def _build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("microfmt")
    pp.add_printer("microfmt::span_sink", r"^microfmt::span_sink$", MicrofmtSpanSinkPrinter)
    pp.add_printer("microfmt::buffer_sink", r"^microfmt::buffer_sink<.*>$", MicrofmtBufferSinkPrinter)
    pp.add_printer("microfmt::c_string_sink", r"^microfmt::c_string_sink<.*>$", MicrofmtCStringSinkPrinter)
    pp.add_printer(
        "microfmt::c_string_span_sink", r"^microfmt::c_string_span_sink$", MicrofmtCStringSpanSinkPrinter
    )
    pp.add_printer("microfmt::counting_sink", r"^microfmt::counting_sink$", MicrofmtCountingSinkPrinter)
    pp.add_printer("microfmt::iterator_sink", r"^microfmt::iterator_sink<.*>$", MicrofmtIteratorSinkPrinter)
    pp.add_printer("microfmt::memory_buffer", r"^microfmt::(detail::)?memory_buffer(_base)?(<.*>)?$",
                    MicrofmtMemoryBufferPrinter)
    pp.add_printer(
        "microfmt::log::basic_logger", r"^microfmt::log::basic_logger<.*>$", MicrofmtLoggerPrinter
    )
    return pp


def register_microfmt_printers(objfile=None):
    """Registers the microfmt pretty printers with GDB.

    Idempotent: re-registering (e.g. because this module was sourced twice,
    or embedded in several object files) simply replaces the previous
    registration under the same name.
    """
    if gdb is None:
        return

    target = objfile if objfile is not None else gdb
    gdb.printing.register_pretty_printer(target, _build_pretty_printer(), replace=True)


if gdb is not None:
    register_microfmt_printers(gdb.current_objfile())
