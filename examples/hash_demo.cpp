// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <microfmt/formatters/hash.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <microfmt/formatters/string.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// User-defined Type with std::hash Specialization
// ============================================================================

struct UserSession {
  uint32_t user_id;
  std::string ip_address;
};

namespace std {
template <> struct hash<UserSession> {
  size_t operator()(const UserSession &s) const noexcept {
    // Simple hash combine: seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >>
    // 2))
    size_t seed = std::hash<uint32_t>{}(s.user_id);
    seed ^= std::hash<std::string>{}(s.ip_address) + 0x9e3779b9 + (seed << 6) +
            (seed >> 2);
    return seed;
  }
};
} // namespace std

namespace microfmt {
template <> struct formatter<UserSession> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const UserSession &s, const sink &out) const noexcept {
    microfmt::format_to(out, "UserSession(id={}, ip={})", s.user_id,
                        s.ip_address);
  }
};
} // namespace microfmt

// ============================================================================
// Demo Scenarios
// ============================================================================

void demo_primitive_and_string_hashing() {
  microfmt::println("=================================================");
  microfmt::println(" 1. Primitive & String std::hash Formatting");
  microfmt::println("=================================================");

  std::string session_key = "bearer_token_abc_123";
  int user_id = 42001;
  double sample_rate = 44.1;

  // Default decimal representation
  microfmt::println("String Dec Hash : {}", microfmt::as_hash(session_key));

  // 64-bit padded hexadecimal representation
  microfmt::println("String Hex Hash : {:#018x}",
                    microfmt::as_hash(session_key));
  microfmt::println("Int Hex Hash    : {:#018x}", microfmt::as_hash(user_id));
  microfmt::println("Double Hex Hash : {:#018x}",
                    microfmt::as_hash(sample_rate));
  microfmt::println();
}

void demo_custom_type_hashing() {
  microfmt::println("=================================================");
  microfmt::println(" 2. User-Defined Struct std::hash Formatting");
  microfmt::println("=================================================");

  UserSession s1{1001, "192.168.1.50"};
  UserSession s2{1002, "10.0.0.1"};

  microfmt::println("Object 1: {}", s1);
  microfmt::println("  Hash (Dec): {}", microfmt::as_hash(s1));
  microfmt::println("  Hash (Hex): {:#018x}", microfmt::as_hash(s1));

  microfmt::println("Object 2: {}", s2);
  microfmt::println("  Hash (Hex): {:#018x}", microfmt::as_hash(s2));
  microfmt::println();
}

void demo_collection_hashing() {
  microfmt::println("=================================================");
  microfmt::println(" 3. Batch Hashing with Range Views");
  microfmt::println("=================================================");

  std::vector<std::string_view> endpoints = {
      "/api/v1/login", "/api/v1/users", "/api/v1/checkout", "/api/v1/health"};

  // Transform collection to hash views on the fly
  std::vector<decltype(microfmt::as_hash(endpoints[0]))> hash_views;
  hash_views.reserve(endpoints.size());
  for (const auto &ep : endpoints) {
    hash_views.push_back(microfmt::as_hash(ep));
  }

  // Join hash digests with custom delimiter and hex specifier
  microfmt::println("Endpoint Hashes:\n  [{:#018x}]",
                    microfmt::join(hash_views, "\n   "));
  microfmt::println();
}

int main() {
  demo_primitive_and_string_hashing();
  demo_custom_type_hashing();
  demo_collection_hashing();
  return 0;
}
