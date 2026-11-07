#pragma once

#include "config.hpp"
#include "guard.hpp"
#include "onebot.hpp"
#include "store.hpp"

#include <string>

namespace satou {

struct BotContext {
  const AppConfig& config;
  GroupStore& store;
  SpamTracker& spam;
  OneBotApi& api;
  std::string self_id;
};

}  // namespace satou
