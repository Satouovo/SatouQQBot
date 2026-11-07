#pragma once

#include "store.hpp"

#include <string>
#include <string_view>

namespace satou {

enum class JoinAction { None, Approve, Reject };

struct JoinDecision {
  JoinAction action = JoinAction::None;
  std::string reason;
};

JoinDecision decide_join(const GroupSettings& settings, std::string_view user_id, std::string_view comment);

}  // namespace satou
