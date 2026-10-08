# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""LLDB data formatters (pretty printers) for the microfmt header-only library.

Mirrors ``tools/gdb/microfmt_printers.py``: the summary strings are kept
identical so output reads the same in both debuggers.

Load it in a running LLDB session::

    (lldb) command script import /path/to/microfmt/tools/lldb/microfmt_printers.py

or automatically, by adding the same line to ``~/.lldbinit`` (or a
project-local ``.lldbinit`` when ``target.load-cwd-lldbinit`` is enabled).

Importing the module calls ``__lldb_init_module``, which registers every
formatter in the ``microfmt`` type category and enables it.

Type-erased sinks/views whose stored data can only be recovered by calling
through a function pointer or an externally-owned abstract interface (e.g.
``microfmt::sink``, ``microfmt::format_args``, ``microfmt::sinks::sbuf_sink``)
are intentionally not covered.
"""

import re

try:
    import lldb
except ImportError:  # pragma: no cover - only importable inside LLDB.
    lldb = None

CATEGORY = "microfmt"
MAX_STRING = 256


# -- helpers ------------------------------------------------------------------


def _m(val, name):
    return val.GetChildMemberWithName(name)


def _u(val):
    return val.GetValueAsUnsigned()


def _template_args(type_name):
    """Splits the top-level template argument list of ``type_name``."""
    start = type_name.find("<")
    end = type_name.rfind(">")
    if start < 0 or end < start:
        return []
    args, depth, cur = [], 0, []
    for ch in type_name[start + 1 : end]:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    args.append("".join(cur).strip())
    return args


def _int_template_arg(val, index):
    """Returns non-type template argument ``index`` of ``val``'s type, or None."""
    for name in (val.GetType().GetName(), val.GetType().GetCanonicalType().GetName()):
        args = _template_args(name or "")
        if index < len(args):
            match = re.match(r"-?\d+", args[index])
            if match:
                return int(match.group(0))
    return None


def _quote(text):
    out = ['"']
    for ch in text:
        if ch in '"\\':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def _char_string(val, length):
    """Renders a ``char*``/``char[N]`` plus length the way LLDB renders C strings."""
    length = int(length)
    if length == 0:
        return '""'
    shown = min(length, MAX_STRING)
    err = lldb.SBError()
    data = val.GetPointeeData(0, shown)
    raw = data.ReadRawData(err, 0, data.GetByteSize()) if data.IsValid() else None
    if not err.Success() or raw is None:
        return "<unreadable>"
    return _quote(raw.decode("utf-8", "replace")) + ("..." if length > shown else "")


# -- summaries ----------------------------------------------------------------


def _span_sink_summary(v):
    buf = _m(v, "m_buf")
    pos = _u(_m(v, "m_pos"))
    return "microfmt::span_sink written %d of %d bytes = %s" % (
        pos,
        _u(_m(buf, "m_size")),
        _char_string(_m(buf, "m_ptr"), pos),
    )


def _buffer_sink_summary(v):
    pos = _u(_m(v, "m_pos"))
    return "microfmt::buffer_sink written %d of %s bytes = %s" % (
        pos,
        _int_template_arg(v, 0),
        _char_string(_m(v, "m_data"), pos),
    )


def _c_string_sink_summary(v):
    pos = _u(_m(v, "m_pos"))
    return "microfmt::c_string_sink written %d of %d bytes = %s" % (
        pos,
        _u(_m(v, "m_max_payload")),
        _char_string(_m(v, "m_data"), pos),
    )


def _c_string_span_sink_summary(v):
    data = _m(v, "m_data")
    if _u(data) == 0:
        return "microfmt::c_string_span_sink [discarding, no buffer]"
    pos = _u(_m(v, "m_pos"))
    return "microfmt::c_string_span_sink written %d of %d bytes = %s" % (
        pos,
        _u(_m(v, "m_max_payload")),
        _char_string(data, pos),
    )


def _counting_sink_summary(v):
    return "microfmt::counting_sink counted %d byte(s)" % _u(_m(v, "m_count"))


def _iterator_sink_summary(v):
    it = _m(v, "m_it")
    text = it.GetValue() or it.GetSummary()
    if text is None:
        text = "?"
    return "microfmt::iterator_sink at %s" % text


def _memory_buffer_summary(v):
    size = _u(_m(v, "m_size"))
    return "microfmt::memory_buffer of length %d, capacity %d = %s" % (
        size,
        _u(_m(v, "m_capacity")),
        _char_string(_m(v, "m_data"), size),
    )


def _logger_summary(v):
    name = _m(v, "name_")
    level = _m(v, "level_")
    level_text = level.GetValue() or str(_u(level))
    if "::" not in level_text:
        level_text = "%s::%s" % (level.GetType().GetName(), level_text)
    return "microfmt::log::basic_logger %s level=%s sinks=%d" % (
        _char_string(_m(name, "data_"), _u(_m(name, "size_"))),
        level_text,
        _u(_m(v, "sink_count_")),
    )


# (type regex, summary function)
_SPECS = [
    ("span_sink", r"^microfmt::span_sink$", _span_sink_summary),
    ("buffer_sink", r"^microfmt::buffer_sink<.*>$", _buffer_sink_summary),
    ("c_string_sink", r"^microfmt::c_string_sink<.*>$", _c_string_sink_summary),
    ("c_string_span_sink", r"^microfmt::c_string_span_sink$", _c_string_span_sink_summary),
    ("counting_sink", r"^microfmt::counting_sink$", _counting_sink_summary),
    ("iterator_sink", r"^microfmt::iterator_sink<.*>$", _iterator_sink_summary),
    ("memory_buffer", r"^microfmt::(detail::)?memory_buffer(_base)?(<.*>)?$", _memory_buffer_summary),
    ("basic_logger", r"^microfmt::log::basic_logger<.*>$", _logger_summary),
]


def _make_summary(fn):
    def summary(valobj, internal_dict):
        try:
            return fn(valobj.GetNonSyntheticValue()) or ""
        except Exception as exc:  # A broken formatter must never break the debugger.
            return "<microfmt formatter error: %s>" % exc

    return summary


def register_microfmt_printers(debugger):
    """Registers the microfmt formatters with ``debugger`` (idempotent)."""
    for name, regex, fn in _SPECS:
        attr = "_summary_" + name
        globals()[attr] = _make_summary(fn)
        debugger.HandleCommand(
            'type summary add --category %s --regex "%s" --python-function %s.%s' % (CATEGORY, regex, __name__, attr)
        )
    debugger.HandleCommand("type category enable %s" % CATEGORY)


def __lldb_init_module(debugger, internal_dict):
    register_microfmt_printers(debugger)
