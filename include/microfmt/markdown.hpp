#pragma once

#include "hexdump.hpp"
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
    print("{}. ", index);
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

private:
  sink m_sink;
};

} // namespace microfmt::md