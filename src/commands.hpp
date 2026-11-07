#pragma once

#include "message.hpp"

#include <string>

namespace satou {

struct BotContext;

struct CommandOutcome {
  bool is_command = false;
  std::string reply;
};

CommandOutcome handle_command(BotContext& ctx, const std::string& group_id, const std::string& user_id, const ParsedMessage& parsed);

}  // namespace satou
