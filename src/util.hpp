#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace satou {

bool is_ascii_space(unsigned char ch);
std::string_view trim_view(std::string_view text);
std::string_view ltrim_view(std::string_view text);
std::string trim_copy(std::string_view text);

std::string json_id(const nlohmann::json& value);
nlohmann::json id_json(const std::string& id);
std::string echo_text(const nlohmann::json& value);

void set_log_sink(std::function<void(const std::string&)> sink);
void log_info(const std::string& message);

}  // namespace satou
