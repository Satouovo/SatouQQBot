#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace satou {

constexpr int kMaxMuteSeconds = 30 * 24 * 60 * 60;

// 空字符串不是合法时长。纯数字按分钟计算，例如 10 等于 10 分钟。
std::optional<int> parse_duration(std::string_view text);
std::string format_seconds(int seconds);

}  // namespace satou
