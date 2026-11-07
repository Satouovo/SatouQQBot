#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace satou {

struct ParsedMessage {
  std::string text;
  std::vector<std::string> at_qq;
  std::optional<std::string> reply_id;
};

ParsedMessage parse_message(const nlohmann::json& message);

}  // namespace satou
