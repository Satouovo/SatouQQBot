#include "approve.hpp"

#include "util.hpp"

namespace satou {
namespace {

bool contains_word(std::string_view text, const std::vector<std::string>& words) {
  for (const auto& word : words) {
    if (!word.empty() && text.find(word) != std::string_view::npos) return true;
  }
  return false;
}

bool listed(const std::vector<std::string>& ids, std::string_view user_id) {
  const auto id = trim_view(user_id);
  for (const auto& item : ids) {
    if (trim_view(item) == id) return true;
  }
  return false;
}

}  // namespace

JoinDecision decide_join(const GroupSettings& settings, std::string_view user_id, std::string_view comment) {
  if (!settings.approve_enabled) return {};
  const std::string reason = settings.approve_reject_reason.empty() ? "未通过入群审批" : settings.approve_reject_reason;
  if (listed(settings.approve_blacklist, user_id)) return {JoinAction::Reject, reason};
  if (contains_word(comment, settings.reject_words)) return {JoinAction::Reject, reason};
  if (settings.approve_words.empty()) {
    if (settings.approve_reject_unmatched) return {JoinAction::Reject, reason};
    return {JoinAction::Approve, {}};
  }
  if (contains_word(comment, settings.approve_words)) return {JoinAction::Approve, {}};
  if (settings.approve_reject_unmatched) return {JoinAction::Reject, reason};
  return {};
}

}  // namespace satou
