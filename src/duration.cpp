#include "duration.hpp"

#include "util.hpp"

#include <cctype>

namespace satou {
namespace {

bool equals_unit(std::string_view unit, std::string_view expected) { return unit == expected; }

int unit_multiplier(std::string_view unit) {
  if (unit.empty()) return 60;
  if (equals_unit(unit, "秒") || equals_unit(unit, "s") || equals_unit(unit, "S")) return 1;
  if (equals_unit(unit, "分") || equals_unit(unit, "分钟") || equals_unit(unit, "m") || equals_unit(unit, "M")) {
    return 60;
  }
  if (equals_unit(unit, "时") || equals_unit(unit, "小时") || equals_unit(unit, "h") || equals_unit(unit, "H")) {
    return 3600;
  }
  if (equals_unit(unit, "天") || equals_unit(unit, "d") || equals_unit(unit, "D")) return 86400;
  return 0;
}

}  // namespace

std::optional<int> parse_duration(std::string_view text) {
  text = trim_view(text);
  if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.front()))) return std::nullopt;

  int value = 0;
  std::size_t index = 0;
  while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
    const int digit = text[index] - '0';
    if (value > 1000000) return std::nullopt;
    value = value * 10 + digit;
    ++index;
  }

  const int multiplier = unit_multiplier(trim_view(text.substr(index)));
  if (multiplier == 0) return std::nullopt;
  const long long seconds = static_cast<long long>(value) * multiplier;
  if (seconds <= 0 || seconds > kMaxMuteSeconds) return std::nullopt;
  return static_cast<int>(seconds);
}

std::string format_seconds(int seconds) {
  if (seconds > 0 && seconds % 86400 == 0) return std::to_string(seconds / 86400) + " 天";
  if (seconds > 0 && seconds % 3600 == 0) return std::to_string(seconds / 3600) + " 小时";
  if (seconds > 0 && seconds % 60 == 0) return std::to_string(seconds / 60) + " 分钟";
  return std::to_string(seconds) + " 秒";
}

}  // namespace satou
