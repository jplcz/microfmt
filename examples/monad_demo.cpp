#include <cstdio>
#include <optional>

#include <microfmt/formatters/monad.hpp>
#include <microfmt/microfmt.hpp>

int main() {
  const auto present = microfmt::format<32>(
      "result={}", std::optional<int>{42});
  const auto absent = microfmt::format<32>(
      "result={}", std::optional<int>{});

  std::printf("%.*s\n%.*s\n", static_cast<int>(present.size()),
              present.view().data(), static_cast<int>(absent.size()),
              absent.view().data());
}
