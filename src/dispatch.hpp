#pragma once

#include <nlohmann/json.hpp>

namespace satou {

struct BotContext;

void handle_event(BotContext& ctx, const nlohmann::json& event);

}  // namespace satou
