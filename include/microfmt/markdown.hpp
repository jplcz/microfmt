// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file markdown.hpp @brief Markdown document-writing helpers. */

#include "formatters/hexdump.hpp"
#include "microfmt.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace microfmt::md {

// ============================================================================
// Table Column Specifications
// ============================================================================

enum class align : uint8_t { left, center, right };

struct column {
  std::string_view title{};
  size_t min_width{0};
  align alignment{align::left};
};

enum class admonition : uint8_t { note, tip, important, warning, caution };

class writer;

template <typename Callable>
writer &details(writer &w, std::string_view summary, Callable &&body,
                bool open = false) noexcept;

// ============================================================================
// Markdown Document Writer (Zero Allocation)
// ============================================================================

class writer {
public:
  explicit constexpr writer(const sink &out) noexcept : m_sink(out) {}

  // ------------------------------------------------------------------------
  // Raw Output & Formatting Primitives
  // ------------------------------------------------------------------------

  writer &write(std::string_view sv) noexcept {
    m_sink.write(sv);
    return *this;
  }

  writer &put(char c) noexcept {
    m_sink.put(c);
    return *this;
  }

  writer &newline() noexcept {
    m_sink.put('\n');
    return *this;
  }

  writer &pad(char c, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
      m_sink.put(c);
    }
    return *this;
  }

  template <typename... Args>
  writer &print(std::string_view fmt, const Args &...args) noexcept {
    format_to(m_sink, fmt, args...);
    return *this;
  }

  template <typename... Args>
  writer &println(std::string_view fmt, const Args &...args) noexcept {
    format_to(m_sink, fmt, args...);
    m_sink.put('\n');
    return *this;
  }

  // ------------------------------------------------------------------------
  // Headings & Dividers
  // ------------------------------------------------------------------------

  template <typename... Args>
  writer &heading(uint8_t level, std::string_view fmt,
                  const Args &...args) noexcept {
    const uint8_t clamped = std::clamp<uint8_t>(level, 1, 6);
    pad('#', clamped);
    put(' ');
    format_to(m_sink, fmt, args...);
    return newline();
  }

  writer &h1(std::string_view title) noexcept {
    return heading(1, "{}", title);
  }
  writer &h2(std::string_view title) noexcept {
    return heading(2, "{}", title);
  }
  writer &h3(std::string_view title) noexcept {
    return heading(3, "{}", title);
  }
  writer &h4(std::string_view title) noexcept {
    return heading(4, "{}", title);
  }

  writer &horizontal_rule() noexcept { return write("---\n"); }

  // ------------------------------------------------------------------------
  // Code Blocks
  // ------------------------------------------------------------------------

  writer &code_block_begin(std::string_view lang = "") noexcept {
    write("```");
    if (!lang.empty()) {
      write(lang);
    }
    return newline();
  }

  writer &code_block_end() noexcept { return write("```\n"); }

  template <typename Callable>
  writer &code_block(std::string_view lang, Callable &&body) noexcept {
    code_block_begin(lang);
    body(*this);
    return code_block_end();
  }

  // ------------------------------------------------------------------------
  // Blockquotes & Lists
  // ------------------------------------------------------------------------

  template <typename... Args>
  writer &blockquote(std::string_view fmt, const Args &...args) noexcept {
    write("> ");
    format_to(m_sink, fmt, args...);
    return newline();
  }

  template <typename... Args>
  writer &list_item(std::string_view fmt, const Args &...args) noexcept {
    write("- ");
    format_to(m_sink, fmt, args...);
    return newline();
  }

  template <typename... Args>
  writer &numbered_item(size_t index, std::string_view fmt,
                        const Args &...args) noexcept {
    format_to(m_sink, MICROFMT_STRING("{}. "), index);
    format_to(m_sink, fmt, args...);
    return newline();
  }

  // ------------------------------------------------------------------------
  // Tables
  // ------------------------------------------------------------------------

  writer &table_header(span<const column> cols) noexcept {
    if (cols.empty())
      return *this;

    // Row 1: Header names
    put('|');
    for (const auto &col : cols) {
      put(' ');
      const size_t width = std::max(col.min_width, col.title.size());
      write(col.title);
      pad(' ', width - col.title.size());
      write(" |");
    }
    newline();

    // Row 2: Alignment delimiters
    put('|');
    for (const auto &col : cols) {
      put(' ');
      const size_t width = std::max(col.min_width, col.title.size());
      switch (col.alignment) {
      case align::center:
        put(':');
        pad('-', width >= 2 ? width - 2 : 1);
        put(':');
        break;
      case align::right:
        pad('-', width >= 1 ? width - 1 : 1);
        put(':');
        break;
      case align::left:
      default:
        put(':');
        pad('-', width >= 1 ? width - 1 : 1);
        break;
      }
      write(" |");
    }
    return newline();
  }

  writer &table_row_begin() noexcept {
    put('|');
    return *this;
  }

  writer &table_cell(std::string_view content, const column &col) noexcept {
    put(' ');
    const size_t width = std::max(col.min_width, col.title.size());
    const size_t len = content.size();

    if (len >= width) {
      write(content);
    } else {
      const size_t pad_total = width - len;
      switch (col.alignment) {
      case align::right:
        pad(' ', pad_total);
        write(content);
        break;
      case align::center: {
        const size_t left = pad_total / 2;
        const size_t right = pad_total - left;
        pad(' ', left);
        write(content);
        pad(' ', right);
        break;
      }
      case align::left:
      default:
        write(content);
        pad(' ', pad_total);
        break;
      }
    }
    write(" |");
    return *this;
  }

  template <size_t ScratchSize = 128, typename... Args>
  writer &table_cell_fmt(const column &col, std::string_view fmt,
                         const Args &...args) noexcept {
    buffer_sink<ScratchSize> scratch;
    format_to(scratch.as_sink(), fmt, args...);
    return table_cell(scratch.view(), col);
  }

  writer &table_row_end() noexcept { return newline(); }

  [[nodiscard]] constexpr sink get_sink() const noexcept { return m_sink; }

  template <typename... Args>
  writer &task_item(bool checked, std::string_view fmt,
                    const Args &...args) noexcept {
    write(checked ? "- [x] " : "- [ ] ");
    format_to(m_sink, fmt, args...);
    return newline();
  }

  template <typename... Args>
  writer &nested_list_item(uint8_t indent_level, std::string_view fmt,
                           const Args &...args) noexcept {
    pad(' ', static_cast<size_t>(indent_level) * 2);
    write("- ");
    format_to(m_sink, fmt, args...);
    return newline();
  }

  template <typename Callable>
  writer &alert(admonition type, Callable &&body) noexcept {
    switch (type) {
    case admonition::note:
      write("> [!NOTE]\n");
      break;
    case admonition::tip:
      write("> [!TIP]\n");
      break;
    case admonition::important:
      write("> [!IMPORTANT]\n");
      break;
    case admonition::warning:
      write("> [!WARNING]\n");
      break;
    case admonition::caution:
      write("> [!CAUTION]\n");
      break;
    }
    // Custom indented blockquote writer proxy
    body(*this);
    return newline();
  }

  template <typename... Args>
  writer &hexdump_block(std::string_view summary,
                        span<const uint8_t> data) noexcept {
    return details(*this, summary, [&](writer &w) {
      w.code_block("text", [&](writer &cw) {
        format_to(cw.get_sink(), MICROFMT_STRING("{}"),
                  microfmt::hexdump(data));
      });
    });
  }

private:
  sink m_sink;
};

// Bold, Italic, Strikethrough, Inline Code, Hyperlink, and Badges
struct bold_view {
  std::string_view text;
};
struct italic_view {
  std::string_view text;
};
struct strike_view {
  std::string_view text;
};
struct code_view {
  std::string_view text;
};
struct link_view {
  std::string_view label;
  std::string_view url;
};
struct image_view {
  std::string_view alt;
  std::string_view url;
};

[[nodiscard]] constexpr bold_view bold(std::string_view s) noexcept {
  return {s};
}
[[nodiscard]] constexpr italic_view italic(std::string_view s) noexcept {
  return {s};
}
[[nodiscard]] constexpr strike_view strike(std::string_view s) noexcept {
  return {s};
}
[[nodiscard]] constexpr code_view code(std::string_view s) noexcept {
  return {s};
}
[[nodiscard]] constexpr link_view link(std::string_view label,
                                       std::string_view url) noexcept {
  return {label, url};
}
[[nodiscard]] constexpr image_view image(std::string_view alt,
                                         std::string_view url) noexcept {
  return {alt, url};
}

} // namespace microfmt::md

template <> struct microfmt::formatter<microfmt::md::bold_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const microfmt::md::bold_view &v,
              const sink &out) const noexcept {
    out.write("**");
    out.write(v.text);
    out.write("**");
  }
};

template <> struct microfmt::formatter<microfmt::md::code_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const microfmt::md::code_view &v,
              const sink &out) const noexcept {
    out.put('`');
    out.write(v.text);
    out.put('`');
  }
};

template <> struct microfmt::formatter<microfmt::md::italic_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const microfmt::md::italic_view &v,
              const sink &out) const noexcept {
    out.put('_');
    out.write(v.text);
    out.put('_');
  }
};

template <> struct microfmt::formatter<microfmt::md::strike_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const microfmt::md::strike_view &v,
              const sink &out) const noexcept {
    out.write("~~");
    out.write(v.text);
    out.write("~~");
  }
};

template <> struct microfmt::formatter<microfmt::md::link_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const microfmt::md::link_view &v,
              const sink &out) const noexcept {
    out.put('[');
    out.write(v.label);
    out.write("](");
    out.write(v.url);
    out.put(')');
  }
};

template <> struct microfmt::formatter<microfmt::md::image_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const microfmt::md::image_view &v,
              const sink &out) const noexcept {
    out.write("![");
    out.write(v.alt);
    out.write("](");
    out.write(v.url);
    out.put(')');
  }
};

namespace microfmt::md {

// ============================================================================
// Collapsible Details Guard (RAII)
// ============================================================================

class details_guard {
public:
  explicit details_guard(writer &w, std::string_view summary,
                         bool open = false) noexcept;
  ~details_guard() noexcept;

  details_guard(const details_guard &) = delete;
  details_guard &operator=(const details_guard &) = delete;
  details_guard(details_guard &&o) noexcept
      : m_writer(std::exchange(o.m_writer, nullptr)) {}

private:
  writer *m_writer{nullptr};
};

// ============================================================================
// details_guard Implementation
// ============================================================================

inline details_guard::details_guard(writer &w, std::string_view summary,
                                    bool open) noexcept
    : m_writer(&w) {
  if (open) {
    m_writer->write("<details open>\n<summary>");
  } else {
    m_writer->write("<details>\n<summary>");
  }
  m_writer->write(summary);
  m_writer->write("</summary>\n\n");
}

inline details_guard::~details_guard() noexcept {
  if (m_writer) {
    m_writer->write("\n</details>\n\n");
  }
}

template <typename Callable>
inline writer &details(writer &w, std::string_view summary, Callable &&body,
                       bool open) noexcept {
  details_guard guard(w, summary, open);
  body(w);
  return w;
}

template <size_t NumCols> class table_writer {
public:
  table_writer(writer &w, span<const column> cols) noexcept
      : m_writer(w), m_cols(cols) {
    m_writer.table_header(m_cols);
  }

  template <typename... Cells>
  table_writer &row(const Cells &...cells) noexcept {
    m_writer.table_row_begin();
    size_t col_idx = 0;
    auto format_cell = [&](const auto &cell) {
      if constexpr (std::is_convertible_v<decltype(cell), std::string_view>) {
        m_writer.table_cell(static_cast<std::string_view>(cell),
                            m_cols[col_idx++]);
      } else {
        m_writer.table_cell_fmt(m_cols[col_idx++], "{}", cell);
      }
    };
    (format_cell(cells), ...);
    m_writer.table_row_end();
    return *this;
  }

private:
  writer &m_writer;
  span<const column> m_cols;
};

} // namespace microfmt::md