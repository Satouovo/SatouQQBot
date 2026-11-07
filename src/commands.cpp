#include "commands.hpp"

#include "context.hpp"
#include "duration.hpp"
#include "perm.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace satou {
namespace {

CommandOutcome reply_of(std::string text) { return CommandOutcome{true, std::move(text)}; }

std::optional<std::string> strip_prefix(std::string_view text, const std::vector<std::string>& prefixes) {
  const std::string* best = nullptr;
  for (const auto& prefix : prefixes) {
    if (text.compare(0, prefix.size(), prefix) != 0) continue;
    if (!best || prefix.size() > best->size()) best = &prefix;
  }
  if (!best) return std::nullopt;
  return std::string(text.substr(best->size()));
}

bool starts_token(std::string_view text, std::string_view token) {
  if (!text.starts_with(token)) return false;
  if (text.size() == token.size()) return true;
  return is_ascii_space(static_cast<unsigned char>(text[token.size()]));
}

bool allow_exact(std::string_view rest) { return trim_view(rest).empty(); }

bool allow_space_or_digit(std::string_view rest) {
  if (rest.empty()) return true;
  const unsigned char ch = static_cast<unsigned char>(rest.front());
  return is_ascii_space(ch) || std::isdigit(ch);
}

bool allow_space_only(std::string_view rest) {
  if (rest.empty()) return true;
  return is_ascii_space(static_cast<unsigned char>(rest.front()));
}

bool allow_switch(std::string_view rest) {
  if (rest.empty() || is_ascii_space(static_cast<unsigned char>(rest.front()))) return true;
  return starts_token(rest, "开") || starts_token(rest, "关");
}

bool allow_word_cmd(std::string_view rest) {
  if (rest.empty() || is_ascii_space(static_cast<unsigned char>(rest.front()))) return true;
  return starts_token(rest, "加") || starts_token(rest, "删") || starts_token(rest, "列") || starts_token(rest, "禁言");
}

bool allow_spam_cmd(std::string_view rest) {
  if (rest.empty() || is_ascii_space(static_cast<unsigned char>(rest.front()))) return true;
  return starts_token(rest, "开") || starts_token(rest, "关") || starts_token(rest, "条数") || starts_token(rest, "窗口") ||
         starts_token(rest, "禁言");
}

struct Cut {
  bool matched = false;
  std::string rest;
};

Cut match_named(std::string_view text, std::string_view name, bool (*allow)(std::string_view)) {
  if (!text.starts_with(name)) return {};
  const auto rest = text.substr(name.size());
  if (!allow(rest)) return {};
  return {true, std::string(ltrim_view(rest))};
}

std::optional<std::string> take_target(const ParsedMessage& parsed, std::string& rest) {
  for (const auto& qq : parsed.at_qq) {
    if (!qq.empty() && qq != "all") return qq;
  }
  std::size_t index = 0;
  while (index < rest.size() && std::isdigit(static_cast<unsigned char>(rest[index]))) ++index;
  if (index >= 5 && index <= 12 &&
      (index == rest.size() || is_ascii_space(static_cast<unsigned char>(rest[index])))) {
    auto id = rest.substr(0, index);
    rest = std::string(ltrim_view(std::string_view(rest).substr(index)));
    return id;
  }
  return std::nullopt;
}

std::vector<std::string> split_words(std::string_view text) {
  std::vector<std::string> words;
  std::string current;
  for (unsigned char ch : text) {
    if (is_ascii_space(ch)) {
      if (!current.empty()) {
        words.push_back(std::move(current));
        current.clear();
      }
    } else {
      current.push_back(static_cast<char>(ch));
    }
  }
  if (!current.empty()) words.push_back(std::move(current));
  return words;
}

bool take_token(std::string& rest, std::string_view token) {
  if (!starts_token(rest, token)) return false;
  rest = std::string(ltrim_view(std::string_view(rest).substr(token.size())));
  return true;
}

std::optional<int> parse_count(std::string_view text) {
  text = trim_view(text);
  if (text.empty()) return std::nullopt;
  int value = 0;
  for (unsigned char ch : text) {
    if (!std::isdigit(ch)) return std::nullopt;
    if (value > 100000) return std::nullopt;
    value = value * 10 + (ch - '0');
  }
  return value;
}

int utf8_codepoints(const std::string& text) {
  int count = 0;
  for (unsigned char ch : text) {
    if ((ch & 0xC0) != 0x80) ++count;
  }
  return count;
}

struct Staff {
  bool ok = false;
  Role actor = Role::Member;
  Role bot = Role::Member;
  std::string error;
};

Staff require_staff(BotContext& ctx, const std::string& group_id, const std::string& user_id) {
  Staff staff;
  if (is_superuser(ctx.config.superusers, user_id)) {
    staff.actor = Role::Superuser;
  } else {
    const auto profile = get_member(ctx.api, group_id, user_id);
    if (!profile.ok) {
      staff.error = "无法确认你的身份：" + profile.error;
      return staff;
    }
    staff.actor = profile.role;
  }
  if (!can_admin(staff.actor)) {
    staff.error = "你没有管理权限";
    return staff;
  }
  if (ctx.self_id.empty()) {
    staff.error = "尚未取得机器人账号";
    return staff;
  }
  const auto bot = get_member(ctx.api, group_id, ctx.self_id);
  if (!bot.ok) {
    staff.error = "无法确认机器人身份：" + bot.error;
    return staff;
  }
  staff.bot = bot.role;
  if (!can_admin(staff.bot)) {
    staff.error = "机器人需要是管理员";
    return staff;
  }
  staff.ok = true;
  return staff;
}

struct Target {
  bool ok = false;
  std::string id;
  std::string error;
};

Target resolve_target(BotContext& ctx, const std::string& group_id, const std::string& actor_id, const ParsedMessage& parsed,
                      std::string& rest, Role actor, Role bot, bool allow_self) {
  Target target;
  const auto id = take_target(parsed, rest);
  if (!id) {
    target.error = "请@要操作的成员";
    return target;
  }
  target.id = *id;
  Role target_role = Role::Member;
  if (is_superuser(ctx.config.superusers, target.id)) {
    target_role = Role::Superuser;
  } else {
    const auto profile = get_member(ctx.api, group_id, target.id);
    if (!profile.ok) {
      target.error = "无法确认对方身份：" + profile.error;
      return target;
    }
    target_role = profile.role;
  }
  const bool self_ok = allow_self && target.id == actor_id;
  if (!self_ok && !can_punish(actor, target_role, bot)) {
    target.error = "不能处罚权限不低于你或机器人的成员";
    return target;
  }
  target.ok = true;
  return target;
}

std::string perform(OneBotApi& api, const std::string& action, const nlohmann::json& params, const std::string& success) {
  const auto result = interpret(api.call(action, params));
  if (!result.ok) return "操作失败：" + result.message;
  return success;
}

std::string help_text(bool manager, int default_mute) {
  if (!manager) return "你不是管理员。可用指令：帮助";
  return std::string("Satou QQBot 群管\n") + "禁 @成员 [时长]  禁言，缺省 " + format_seconds(default_mute) +
         "。例：禁 @成员 10分\n"
         "解 @成员  解除禁言\n"
         "全禁 / 解全禁  全体禁言开关\n"
         "踢 @成员  移出群聊\n"
         "黑 @成员  移出并拒绝再次加群\n"
         "撤回  引用一条消息后撤回\n"
         "改 @成员 名片  修改群名片，留空则清除\n"
         "欢迎 开|关  入群欢迎\n"
         "欢迎语 文本  可用 {nickname} {user_id} {card}\n"
         "退群通知 开|关\n"
         "违禁词 加|删|列|禁言\n"
         "刷屏 开|关|条数|窗口|禁言\n"
         "帮助";
}

std::string list_words(const GroupSettings& settings) {
  if (settings.words.empty()) return "当前没有违禁词，命中后禁言 " + format_seconds(settings.word_mute_seconds);
  std::string text = "违禁词：";
  for (std::size_t i = 0; i < settings.words.size(); ++i) {
    if (i != 0) text += "、";
    text += settings.words[i];
  }
  text += "。命中后禁言 " + format_seconds(settings.word_mute_seconds);
  return text;
}

std::string spam_status(const GroupSettings& settings) {
  return std::string("刷屏：") + (settings.spam_enabled ? "开" : "关") + "，" + std::to_string(settings.spam_limit) + " 条 / " +
         std::to_string(settings.spam_window_seconds) + " 秒，禁言 " + format_seconds(settings.spam_mute_seconds);
}

CommandOutcome cmd_whole_ban(BotContext& ctx, const std::string& group_id, const std::string& user_id, bool enable) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  return reply_of(perform(ctx.api, "set_group_whole_ban", {{"group_id", id_json(group_id)}, {"enable", enable}},
                          enable ? "已开启全体禁言" : "已关闭全体禁言"));
}

CommandOutcome cmd_ban(BotContext& ctx, const std::string& group_id, const std::string& user_id, const ParsedMessage& parsed,
                       std::string rest, bool lift) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  const auto target = resolve_target(ctx, group_id, user_id, parsed, rest, staff.actor, staff.bot, false);
  if (!target.ok) return reply_of(target.error);
  int seconds = 0;
  if (!lift) {
    if (ctx.store.contains(group_id) || ctx.store.contains("*")) seconds = ctx.store.get(group_id).default_mute_seconds;
    else seconds = ctx.config.default_mute_seconds;
    if (!rest.empty()) {
      const auto duration = parse_duration(rest);
      if (!duration) return reply_of("时长格式不正确，例如 10分、30秒、1小时、1天");
      seconds = *duration;
    }
  } else if (!rest.empty()) {
    return reply_of("用法：解 @成员");
  }
  const std::string success = lift ? "已解除 " + target.id + " 的禁言" : "已禁言 " + target.id + " " + format_seconds(seconds);
  const auto result = interpret(ctx.api.call(
      "set_group_ban", {{"group_id", id_json(group_id)}, {"user_id", id_json(target.id)}, {"duration", seconds}}));
  if (!result.ok) return reply_of("操作失败：" + result.message);
  log_info("群 " + group_id + " " + (lift ? "解禁 " : "禁言 ") + target.id);
  return reply_of(success);
}

CommandOutcome cmd_kick(BotContext& ctx, const std::string& group_id, const std::string& user_id, const ParsedMessage& parsed,
                        std::string rest, bool reject) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  const auto target = resolve_target(ctx, group_id, user_id, parsed, rest, staff.actor, staff.bot, false);
  if (!target.ok) return reply_of(target.error);
  if (!rest.empty()) return reply_of(reject ? "用法：黑 @成员" : "用法：踢 @成员");
  const auto result = interpret(ctx.api.call("set_group_kick", {{"group_id", id_json(group_id)},
                                                                  {"user_id", id_json(target.id)},
                                                                  {"reject_add_request", reject}}));
  if (!result.ok) return reply_of("操作失败：" + result.message);
  log_info("群 " + group_id + " " + (reject ? "拉黑 " : "踢出 ") + target.id);
  return reply_of(reject ? "已移出并拒绝再次加群 " + target.id : "已移出群聊 " + target.id);
}

CommandOutcome cmd_recall(BotContext& ctx, const std::string& group_id, const std::string& user_id, const ParsedMessage& parsed) {
  if (!parsed.reply_id || parsed.reply_id->empty()) return reply_of("请引用要撤回的消息");
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  const auto message = interpret(ctx.api.call("get_msg", {{"message_id", id_json(*parsed.reply_id)}}));
  if (!message.ok || !message.data.is_object()) return reply_of("找不到被引用的消息");
  if (message.data.contains("group_id")) {
    const auto quoted_group = json_id(message.data.at("group_id"));
    if (!quoted_group.empty() && quoted_group != group_id) return reply_of("不能撤回其他群的消息");
  }
  std::string sender;
  if (message.data.contains("sender") && message.data.at("sender").is_object() && message.data.at("sender").contains("user_id")) {
    sender = json_id(message.data.at("sender").at("user_id"));
  } else if (message.data.contains("user_id")) {
    sender = json_id(message.data.at("user_id"));
  }
  if (sender.empty()) return reply_of("无法确认原消息发送者，已取消");

  Role target_role = Role::Member;
  if (is_superuser(ctx.config.superusers, sender)) {
    target_role = Role::Superuser;
  } else if (sender != ctx.self_id) {
    const auto profile = get_member(ctx.api, group_id, sender);
    if (!profile.ok) return reply_of("无法确认对方身份：" + profile.error);
    target_role = profile.role;
  } else {
    target_role = staff.bot;
  }
  const bool allowed = sender == user_id || sender == ctx.self_id || can_punish(staff.actor, target_role, staff.bot);
  if (!allowed) return reply_of("不能处罚权限不低于你或机器人的成员");
  return reply_of(perform(ctx.api, "delete_msg", {{"message_id", id_json(*parsed.reply_id)}}, "已撤回"));
}

CommandOutcome cmd_card(BotContext& ctx, const std::string& group_id, const std::string& user_id, const ParsedMessage& parsed,
                        std::string rest) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  const auto target = resolve_target(ctx, group_id, user_id, parsed, rest, staff.actor, staff.bot, true);
  if (!target.ok) return reply_of(target.error);
  const std::string card = trim_copy(rest);
  if (utf8_codepoints(card) > 60) return reply_of("名片不能超过 60 个字");
  return reply_of(perform(ctx.api, "set_group_card",
                          {{"group_id", id_json(group_id)}, {"user_id", id_json(target.id)}, {"card", card}},
                          card.empty() ? "已清除群名片" : "已修改群名片"));
}

CommandOutcome cmd_welcome(BotContext& ctx, const std::string& group_id, const std::string& user_id, const std::string& rest) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  if (rest.empty()) return reply_of(ctx.store.get(group_id).welcome_enabled ? "入群欢迎：开" : "入群欢迎：关");
  if (rest != "开" && rest != "关") return reply_of("用法：欢迎 开 或 欢迎 关");
  const bool enabled = rest == "开";
  if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.welcome_enabled = enabled; })) {
    return reply_of("保存群配置失败");
  }
  return reply_of(enabled ? "已开启入群欢迎" : "已关闭入群欢迎");
}

CommandOutcome cmd_welcome_text(BotContext& ctx, const std::string& group_id, const std::string& user_id, const std::string& rest) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  if (rest.empty()) return reply_of("当前欢迎语：" + ctx.store.get(group_id).welcome_text);
  if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.welcome_text = rest; })) return reply_of("保存群配置失败");
  return reply_of("已更新欢迎语");
}

CommandOutcome cmd_leave(BotContext& ctx, const std::string& group_id, const std::string& user_id, const std::string& rest) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  if (rest.empty()) return reply_of(ctx.store.get(group_id).leave_enabled ? "退群通知：开" : "退群通知：关");
  if (rest != "开" && rest != "关") return reply_of("用法：退群通知 开 或 退群通知 关");
  const bool enabled = rest == "开";
  if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.leave_enabled = enabled; })) return reply_of("保存群配置失败");
  return reply_of(enabled ? "已开启退群通知" : "已关闭退群通知");
}

CommandOutcome cmd_words(BotContext& ctx, const std::string& group_id, const std::string& user_id, std::string rest) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  if (rest.empty()) return reply_of(list_words(ctx.store.get(group_id)));
  if (take_token(rest, "列")) {
    if (!rest.empty()) return reply_of("用法：违禁词 列");
    return reply_of(list_words(ctx.store.get(group_id)));
  }
  if (take_token(rest, "禁言")) {
    const auto duration = parse_duration(rest);
    if (!duration) return reply_of("用法：违禁词 禁言 10分");
    if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.word_mute_seconds = *duration; })) {
      return reply_of("保存群配置失败");
    }
    return reply_of("违禁词命中后禁言 " + format_seconds(*duration));
  }
  const bool add = take_token(rest, "加");
  const bool remove = !add && take_token(rest, "删");
  if (!add && !remove) return reply_of("用法：违禁词 加|删|列|禁言");
  const auto words = split_words(rest);
  if (words.empty()) return reply_of(add ? "用法：违禁词 加 词语" : "用法：违禁词 删 词语");
  int changed = 0;
  const bool saved = ctx.store.update(group_id, [&](GroupSettings& settings) {
    for (const auto& word : words) {
      auto found = std::find(settings.words.begin(), settings.words.end(), word);
      if (add && found == settings.words.end()) {
        settings.words.push_back(word);
        ++changed;
      } else if (remove && found != settings.words.end()) {
        settings.words.erase(found);
        ++changed;
      }
    }
  });
  if (!saved) return reply_of("保存群配置失败");
  if (changed == 0) return reply_of(add ? "这些违禁词已经存在" : "没有这些违禁词");
  return reply_of(add ? "已添加 " + std::to_string(changed) + " 个违禁词" : "已删除 " + std::to_string(changed) + " 个违禁词");
}

CommandOutcome cmd_spam(BotContext& ctx, const std::string& group_id, const std::string& user_id, std::string rest) {
  const auto staff = require_staff(ctx, group_id, user_id);
  if (!staff.ok) return reply_of(staff.error);
  if (rest.empty()) return reply_of(spam_status(ctx.store.get(group_id)));
  if (rest == "开" || rest == "关") {
    const bool enabled = rest == "开";
    if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.spam_enabled = enabled; })) return reply_of("保存群配置失败");
    return reply_of(enabled ? "已开启刷屏检测" : "已关闭刷屏检测");
  }
  if (take_token(rest, "条数")) {
    const auto count = parse_count(rest);
    if (!count || *count < 2 || *count > 100) return reply_of("用法：刷屏 条数 5（2 到 100）");
    if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.spam_limit = *count; })) return reply_of("保存群配置失败");
    return reply_of("刷屏条数已设为 " + std::to_string(*count));
  }
  if (take_token(rest, "窗口")) {
    const auto seconds = parse_count(rest);
    if (!seconds || *seconds < 1 || *seconds > 3600) return reply_of("用法：刷屏 窗口 8（秒，1 到 3600）");
    if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.spam_window_seconds = *seconds; })) {
      return reply_of("保存群配置失败");
    }
    return reply_of("刷屏窗口已设为 " + std::to_string(*seconds) + " 秒");
  }
  if (take_token(rest, "禁言")) {
    const auto duration = parse_duration(rest);
    if (!duration) return reply_of("用法：刷屏 禁言 60秒");
    if (!ctx.store.update(group_id, [&](GroupSettings& settings) { settings.spam_mute_seconds = *duration; })) {
      return reply_of("保存群配置失败");
    }
    return reply_of("刷屏禁言已设为 " + format_seconds(*duration));
  }
  return reply_of("用法：刷屏 开|关|条数|窗口|禁言");
}

CommandOutcome cmd_help(BotContext& ctx, const std::string& group_id, const std::string& user_id) {
  Role role = Role::Member;
  if (is_superuser(ctx.config.superusers, user_id)) {
    role = Role::Superuser;
  } else {
    const auto profile = get_member(ctx.api, group_id, user_id);
    if (!profile.ok) return reply_of("无法确认你的身份：" + profile.error);
    role = profile.role;
  }
  const int mute_seconds = ctx.store.contains(group_id) || ctx.store.contains("*") ? ctx.store.get(group_id).default_mute_seconds
                                                                                  : ctx.config.default_mute_seconds;
  return reply_of(help_text(can_admin(role), mute_seconds));
}

}  // namespace

CommandOutcome handle_command(BotContext& ctx, const std::string& group_id, const std::string& user_id, const ParsedMessage& parsed) {
  const auto prefixed = strip_prefix(trim_view(parsed.text), ctx.config.command_prefixes);
  if (!prefixed) return {};
  const auto line = trim_copy(*prefixed);

  if (const auto cut = match_named(line, "解全禁", allow_exact); cut.matched) return cmd_whole_ban(ctx, group_id, user_id, false);
  if (const auto cut = match_named(line, "全禁", allow_exact); cut.matched) return cmd_whole_ban(ctx, group_id, user_id, true);
  if (const auto cut = match_named(line, "欢迎语", allow_space_only); cut.matched) return cmd_welcome_text(ctx, group_id, user_id, cut.rest);
  if (const auto cut = match_named(line, "退群通知", allow_switch); cut.matched) return cmd_leave(ctx, group_id, user_id, cut.rest);
  if (const auto cut = match_named(line, "违禁词", allow_word_cmd); cut.matched) return cmd_words(ctx, group_id, user_id, cut.rest);
  if (const auto cut = match_named(line, "刷屏", allow_spam_cmd); cut.matched) return cmd_spam(ctx, group_id, user_id, cut.rest);
  if (const auto cut = match_named(line, "撤回", allow_exact); cut.matched) return cmd_recall(ctx, group_id, user_id, parsed);
  if (const auto cut = match_named(line, "帮助", allow_exact); cut.matched) return cmd_help(ctx, group_id, user_id);
  if (const auto cut = match_named(line, "禁", allow_space_or_digit); cut.matched) {
    return cmd_ban(ctx, group_id, user_id, parsed, cut.rest, false);
  }
  if (const auto cut = match_named(line, "解", allow_space_or_digit); cut.matched) {
    return cmd_ban(ctx, group_id, user_id, parsed, cut.rest, true);
  }
  if (const auto cut = match_named(line, "踢", allow_space_or_digit); cut.matched) {
    return cmd_kick(ctx, group_id, user_id, parsed, cut.rest, false);
  }
  if (const auto cut = match_named(line, "黑", allow_space_or_digit); cut.matched) {
    return cmd_kick(ctx, group_id, user_id, parsed, cut.rest, true);
  }
  if (const auto cut = match_named(line, "改", allow_space_only); cut.matched) return cmd_card(ctx, group_id, user_id, parsed, cut.rest);
  if (const auto cut = match_named(line, "欢迎", allow_switch); cut.matched) return cmd_welcome(ctx, group_id, user_id, cut.rest);
  return {};
}

}  // namespace satou
