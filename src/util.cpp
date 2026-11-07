#include "util.hpp"

#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace satou {

bool is_ascii_space(unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string_view ltrim_view(std::string_view text) {
  while (!text.empty() && is_ascii_space(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  return text;
}

std::string_view trim_view(std::string_view text) {
  text = ltrim_view(text);
  while (!text.empty() && is_ascii_space(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

std::string trim_copy(std::string_view text) { return std::string(trim_view(text)); }

std::string json_id(const nlohmann::json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (value.is_number_unsigned()) return std::to_string(value.get<std::uint64_t>());
  if (value.is_number_integer()) return std::to_string(value.get<std::int64_t>());
  if (value.is_number_float()) return std::to_string(static_cast<std::int64_t>(value.get<double>()));
  return {};
}

nlohmann::json id_json(const std::string& id) {
  if (id.empty()) return id;
  try {
    std::size_t used = 0;
    const long long number = std::stoll(id, &used, 10);
    if (used == id.size()) return number;
  } catch (const std::exception&) {
  }
  return id;
}

std::string echo_text(const nlohmann::json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (value.is_number_unsigned()) return std::to_string(value.get<std::uint64_t>());
  if (value.is_number_integer()) return std::to_string(value.get<std::int64_t>());
  return value.dump();
}

namespace {

std::mutex g_log_mu;
std::function<void(const std::string&)> g_log_sink;

}  // namespace

void set_log_sink(std::function<void(const std::string&)> sink) {
  std::lock_guard lock(g_log_mu);
  g_log_sink = std::move(sink);
}

void log_info(const std::string& message) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t ticks = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_s(&local, &ticks);
  std::ostringstream formatted;
  formatted << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << ' ' << message;
  const std::string line = formatted.str();

  std::function<void(const std::string&)> sink;
  {
    std::lock_guard lock(g_log_mu);
    sink = g_log_sink;
  }
  if (sink) sink(line);
  else std::cout << line << std::endl;
}

}  // namespace satou
