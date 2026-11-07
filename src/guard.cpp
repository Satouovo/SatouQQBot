#include "guard.hpp"

#include "approve.hpp"
#include "context.hpp"
#include "duration.hpp"
#include "perm.hpp"
#include "util.hpp"

#include <map>

namespace satou {
namespace {

std::string spam_key(const std::string& group_id, const std::string& user_id) { return group_id + '\n' + user_id; }

std::string apply_template(std::string_view templ, const std::map<std::string, std::string>& values) {
  std::string out;
  for (std::size_t i = 0; i < templ.size();) {
    if (templ[i] == '{') {
      const auto end = templ.find('}', i + 1);
      if (end != std::string_view::npos) {
        const auto key = std::string(templ.substr(i + 1, end - i - 1));
        const auto found = values.find(key);
        if (found != values.end()) {
          out += found->second;
          i = end + 1;
          continue;
        }
      }
    }
    out.push_back(templ[i]);
    ++i;
  }
  return out;
}

Role effective_member_role(BotContext& ctx, const std::string& group_id, const std::string& user_id, bool& known) {
  if (is_superuser(ctx.config.superusers, user_id)) {
    known = true;
    return Role::Superuser;
  }
  const auto profile = get_member(ctx.api, group_id, user_id);
  known = profile.ok;
  return profile.ok ? profile.role : Role::Member;
}

void delete_and_ban(BotContext& ctx, const std::string& group_id, const std::string& user_id, const nlohmann::json& message_id,
                    int seconds, const std::string& notice) {
  if (!message_id.is_null()) {
    const auto deleted = interpret(ctx.api.call("delete_msg", {{"message_id", message_id}}));
    if (!deleted.ok) log_info("撤回失败：" + deleted.message);
  }
  if (seconds > 0) {
    const auto banned = interpret(ctx.api.call(
        "set_group_ban", {{"group_id", id_json(group_id)}, {"user_id", id_json(user_id)}, {"duration", seconds}}));
    if (!banned.ok) {
      send_group_text(ctx.api, group_id, "自动处罚失败：" + banned.message);
      return;
    }
  }
  send_group_text(ctx.api, group_id, notice);
}

}  // namespace

bool SpamTracker::observe(const std::string& group_id, const std::string& user_id, int limit, int window_seconds,
                          std::chrono::steady_clock::time_point now) {
  if (limit < 2 || window_seconds <= 0) return false;
  auto& hits = hits_[spam_key(group_id, user_id)];
  const auto window = std::chrono::seconds(window_seconds);
  while (!hits.empty() && now - hits.front() >= window) hits.pop_front();
  hits.push_back(now);
  if (static_cast<int>(hits.size()) >= limit) {
    hits.clear();
    return true;
  }
  return false;
}

void SpamTracker::clear(const std::string& group_id, const std::string& user_id) { hits_.erase(spam_key(group_id, user_id)); }

std::optional<std::string> find_banned_word(std::string_view text, const std::vector<std::string>& words) {
  const std::string* found = nullptr;
  for (const auto& word : words) {
    if (word.empty() || text.find(word) == std::string_view::npos) continue;
    if (!found || word.size() > found->size()) found = &word;
  }
  if (!found) return std::nullopt;
  return *found;
}

void handle_notice(BotContext& ctx, const nlohmann::json& event) {
  const auto type = event.value("notice_type", "");
  if (!event.contains("group_id") || !event.contains("user_id")) return;
  const auto group_id = json_id(event.at("group_id"));
  const auto user_id = json_id(event.at("user_id"));
  if (group_id.empty() || user_id.empty()) return;
  const auto settings = ctx.store.get(group_id);

  if (type == "group_increase") {
    if (user_id == ctx.self_id || !settings.welcome_enabled) return;
    const auto profile = get_member(ctx.api, group_id, user_id);
    const std::string nickname = profile.ok && !profile.nickname.empty() ? profile.nickname : user_id;
    std::string card = profile.ok ? profile.card : "";
    if (card.empty()) card = nickname;
    const auto text = apply_template(settings.welcome_text, {{"nickname", nickname}, {"user_id", user_id}, {"card", card}});
    if (!text.empty()) send_group_text(ctx.api, group_id, text);
    return;
  }

  if (type == "group_decrease") {
    if (user_id == ctx.self_id) {
      log_info("机器人已不在群 " + group_id);
      return;
    }
    if (!settings.leave_enabled) return;
    std::string nickname;
    const auto stranger = interpret(ctx.api.call("get_stranger_info", {{"user_id", id_json(user_id)}}));
    if (stranger.ok && stranger.data.is_object() && stranger.data.contains("nickname") && stranger.data.at("nickname").is_string()) {
      nickname = stranger.data.at("nickname").get<std::string>();
    }
    const std::string who = nickname.empty() || nickname == user_id ? user_id : nickname + "（" + user_id + "）";
    const auto sub = event.value("sub_type", "");
    const char* action = "离开了群聊";
    if (sub == "leave") action = "退出了群聊";
    else if (sub == "kick" || sub == "kick_me") action = "被移出群聊";
    send_group_text(ctx.api, group_id, who + action);
  }
}

bool handle_automod(BotContext& ctx, const std::string& group_id, const std::string& user_id, const std::string& text,
                    const nlohmann::json& message_id) {
  const auto settings = ctx.store.get(group_id);
  if (const auto word = find_banned_word(text, settings.words)) {
    bool known = false;
    const Role role = effective_member_role(ctx, group_id, user_id, known);
    if (!known || can_admin(role)) return false;
    log_info("群 " + group_id + " 违禁词命中 " + user_id);
    delete_and_ban(ctx, group_id, user_id, message_id, settings.word_mute_seconds,
                   "已撤回违禁内容并禁言 " + user_id + " " + format_seconds(settings.word_mute_seconds));
    return true;
  }

  if (!settings.spam_enabled) return false;
  if (!ctx.spam.observe(group_id, user_id, settings.spam_limit, settings.spam_window_seconds, std::chrono::steady_clock::now())) {
    return false;
  }
  bool known = false;
  const Role role = effective_member_role(ctx, group_id, user_id, known);
  if (!known || can_admin(role)) {
    ctx.spam.clear(group_id, user_id);
    return false;
  }
  log_info("群 " + group_id + " 刷屏 " + user_id);
  delete_and_ban(ctx, group_id, user_id, message_id, settings.spam_mute_seconds,
                 "已因刷屏禁言 " + user_id + " " + format_seconds(settings.spam_mute_seconds));
  return true;
}

void handle_group_request(BotContext& ctx, const nlohmann::json& event) {
  if (event.value("request_type", "") != "group" || event.value("sub_type", "") != "add") return;
  if (!event.contains("group_id") || !event.contains("user_id") || !event.contains("flag")) return;
  const auto group_id = json_id(event.at("group_id"));
  const auto user_id = json_id(event.at("user_id"));
  const auto& flag_value = event.at("flag");
  const std::string flag = flag_value.is_string() ? flag_value.get<std::string>() : json_id(flag_value);
  if (group_id.empty() || user_id.empty() || flag.empty()) return;
  const std::string comment = event.contains("comment") && event.at("comment").is_string() ? event.at("comment").get<std::string>() : "";
  const auto decision = decide_join(ctx.store.get(group_id), user_id, comment);
  if (decision.action == JoinAction::None) {
    log_info("群 " + group_id + " 入群申请 " + user_id + " 未自动处理");
    return;
  }
  const bool approve = decision.action == JoinAction::Approve;
  const auto result = interpret(ctx.api.call("set_group_add_request", {{"flag", flag},
                                                                        {"sub_type", "add"},
                                                                        {"approve", approve},
                                                                        {"reason", decision.reason}}));
  if (!result.ok) {
    log_info("群 " + group_id + " 处理入群申请失败：" + result.message);
    return;
  }
  log_info("群 " + group_id + (approve ? " 已同意 " : " 已拒绝 ") + user_id + " 入群");
}

}  // namespace satou
