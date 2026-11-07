#include "approve.hpp"
#include "commands.hpp"
#include "config.hpp"
#include "context.hpp"
#include "dispatch.hpp"
#include "duration.hpp"
#include "guard.hpp"
#include "message.hpp"
#include "perm.hpp"
#include "store.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_failed = 0;

void check(bool condition, const char* file, int line, const std::string& text) {
  if (condition) return;
  std::cerr << "FAIL " << file << ":" << line << " " << text << '\n';
  ++g_failed;
}

#define CHECK(cond) check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(a, b) check((a) == (b), __FILE__, __LINE__, std::string(#a) + " == " + #b + " | " + std::to_string(a) + " vs " + std::to_string(b))
#define CHECK_STREQ(a, b) check((a) == (b), __FILE__, __LINE__, std::string(#a) + " == " + #b + " | [" + (a) + "] vs [" + (b) + "]")

class FakeApi : public satou::OneBotApi {
 public:
  nlohmann::json call(const std::string& action, const nlohmann::json& params) override {
    calls.emplace_back(action, params);
    if (fail_actions.count(action) != 0) {
      return {{"status", "failed"}, {"retcode", 100}, {"data", nullptr}, {"message", "权限不足"}};
    }
    if (action == "get_group_member_info") {
      const auto user = satou::json_id(params.at("user_id"));
      const auto found = roles.find(user);
      const std::string role = found == roles.end() ? "member" : found->second;
      return {{"status", "ok"},
              {"retcode", 0},
              {"data", {{"user_id", user}, {"role", role}, {"nickname", "昵称" + user}, {"card", "名片" + user}}},
              {"message", ""}};
    }
    if (action == "get_stranger_info") {
      return {{"status", "ok"}, {"retcode", 0}, {"data", {{"nickname", "路人"}}}, {"message", ""}};
    }
    if (action == "get_msg") {
      return {{"status", "ok"},
              {"retcode", 0},
              {"data", {{"group_id", quoted_group}, {"sender", {{"user_id", quoted_user}}}}},
              {"message", ""}};
    }
    return {{"status", "ok"}, {"retcode", 0}, {"data", nullptr}, {"message", ""}};
  }

  std::map<std::string, std::string> roles;
  std::map<std::string, int> fail_actions;
  std::vector<std::pair<std::string, nlohmann::json>> calls;
  std::string quoted_user = "2";
  std::string quoted_group = "9";
};

struct Env {
  satou::AppConfig config;
  satou::GroupStore store;
  satou::SpamTracker spam;
  FakeApi api;
  satou::BotContext ctx;

  explicit Env(const std::filesystem::path& path) : store(path), ctx(config, store, spam, api, "900") {
    api.roles["900"] = "admin";
    api.roles["1"] = "admin";
    api.roles["2"] = "member";
    api.roles["3"] = "owner";
    api.roles["8"] = "admin";
  }
};

nlohmann::json text_message(const std::string& text) {
  return nlohmann::json::array({{{"type", "text"}, {"data", {{"text", text}}}}});
}

const nlohmann::json* find_call(const FakeApi& api, const std::string& action) {
  for (const auto& call : api.calls) {
    if (call.first == action) return &call.second;
  }
  return nullptr;
}

int count_call(const FakeApi& api, const std::string& action) {
  return static_cast<int>(std::count_if(api.calls.begin(), api.calls.end(), [&](const auto& call) { return call.first == action; }));
}

std::vector<std::string> replies(const FakeApi& api) {
  std::vector<std::string> out;
  for (const auto& call : api.calls) {
    if (call.first == "send_group_msg") out.push_back(call.second.at("message").get<std::string>());
  }
  return out;
}

void test_join_approval() {
  satou::GroupSettings settings;
  CHECK(satou::decide_join(settings, "10001", "你好").action == satou::JoinAction::None);
  settings.approve_enabled = true;
  CHECK(satou::decide_join(settings, "10001", "").action == satou::JoinAction::Approve);
  settings.approve_words = {"朋友"};
  CHECK(satou::decide_join(settings, "10001", "我是朋友").action == satou::JoinAction::Approve);
  CHECK(satou::decide_join(settings, "10001", "路过").action == satou::JoinAction::None);
  settings.approve_reject_unmatched = true;
  CHECK(satou::decide_join(settings, "10001", "路过").action == satou::JoinAction::Reject);
  settings.reject_words = {"广告"};
  CHECK(satou::decide_join(settings, "10001", "朋友广告").action == satou::JoinAction::Reject);
  settings.approve_blacklist = {"10001"};
  CHECK(satou::decide_join(settings, "10001", "朋友").action == satou::JoinAction::Reject);
  settings.approve_enabled = false;
  CHECK(satou::decide_join(settings, "10001", "广告").action == satou::JoinAction::None);
}

void test_duration() {
  CHECK_EQ(satou::parse_duration("10分").value_or(-1), 600);
  CHECK_EQ(satou::parse_duration("10分钟").value_or(-1), 600);
  CHECK_EQ(satou::parse_duration("30秒").value_or(-1), 30);
  CHECK_EQ(satou::parse_duration("1小时").value_or(-1), 3600);
  CHECK_EQ(satou::parse_duration("1时").value_or(-1), 3600);
  CHECK_EQ(satou::parse_duration("2天").value_or(-1), 172800);
  CHECK_EQ(satou::parse_duration("10").value_or(-1), 600);
  CHECK_EQ(satou::parse_duration("30天").value_or(-1), 2592000);
  CHECK(!satou::parse_duration("").has_value());
  CHECK(!satou::parse_duration("十").has_value());
  CHECK(!satou::parse_duration("0秒").has_value());
  CHECK(!satou::parse_duration("31天").has_value());
  CHECK(!satou::parse_duration("10分多余").has_value());
}

void test_permission() {
  using satou::Role;
  CHECK(!satou::can_admin(Role::Member));
  CHECK(satou::can_admin(Role::Admin));
  CHECK(satou::can_punish(Role::Admin, Role::Member, Role::Admin));
  CHECK(!satou::can_punish(Role::Admin, Role::Admin, Role::Admin));
  CHECK(!satou::can_punish(Role::Admin, Role::Owner, Role::Admin));
  CHECK(satou::can_punish(Role::Owner, Role::Admin, Role::Owner));
  CHECK(!satou::can_punish(Role::Owner, Role::Owner, Role::Owner));
  CHECK(satou::can_punish(Role::Superuser, Role::Admin, Role::Owner));
  CHECK(!satou::can_punish(Role::Superuser, Role::Admin, Role::Admin));
  CHECK(!satou::can_punish(Role::Superuser, Role::Superuser, Role::Owner));
  CHECK(satou::role_from_group("owner") == Role::Owner);
  CHECK(satou::role_from_group("admin") == Role::Admin);
  CHECK(satou::role_from_group("member") == Role::Member);
}

void test_message() {
  const auto array = satou::parse_message(nlohmann::json::array({
      {{"type", "text"}, {"data", {{"text", "禁 "}}}},
      {{"type", "at"}, {"data", {{"qq", "12345"}}}},
      {{"type", "text"}, {"data", {{"text", " 10分"}}}},
      {{"type", "reply"}, {"data", {{"id", 99}}}},
  }));
  CHECK_STREQ(array.text, std::string("禁  10分"));
  CHECK_EQ(array.at_qq.size(), 1u);
  CHECK_STREQ(array.at_qq[0], std::string("12345"));
  CHECK(array.reply_id.has_value());
  CHECK_STREQ(*array.reply_id, std::string("99"));

  const auto cq = satou::parse_message("[CQ:at,qq=123][CQ:reply,id=9]你好");
  CHECK_STREQ(cq.text, std::string("你好"));
  CHECK_EQ(cq.at_qq.size(), 1u);
  CHECK_STREQ(cq.at_qq[0], std::string("123"));
  CHECK(cq.reply_id.has_value());
  CHECK_STREQ(*cq.reply_id, std::string("9"));
}

void test_words_and_spam() {
  const std::vector<std::string> words{"", "广告", "广告链接"};
  const auto matched = satou::find_banned_word("这是广告链接", words);
  CHECK(matched.has_value());
  CHECK_STREQ(*matched, std::string("广告链接"));
  CHECK(!satou::find_banned_word("正常聊天", words).has_value());

  satou::SpamTracker spam;
  const auto start = std::chrono::steady_clock::time_point{};
  CHECK(!spam.observe("1", "2", 3, 10, start));
  CHECK(!spam.observe("1", "2", 3, 10, start + std::chrono::seconds(1)));
  CHECK(spam.observe("1", "2", 3, 10, start + std::chrono::seconds(2)));
  CHECK(!spam.observe("1", "2", 3, 10, start + std::chrono::seconds(3)));
  CHECK(!spam.observe("9", "2", 3, 10, start));
  CHECK(!spam.observe("9", "2", 3, 10, start + std::chrono::seconds(20)));
}

void test_config_and_store(const std::filesystem::path& root) {
  CHECK_STREQ(satou::build_websocket_url("ws://127.0.0.1:3011", ""), std::string("ws://127.0.0.1:3011"));
  CHECK_STREQ(satou::build_websocket_url("ws://127.0.0.1:3011", "a b"), std::string("ws://127.0.0.1:3011/?access_token=a%20b"));
  CHECK_STREQ(satou::build_websocket_url("ws://127.0.0.1:3011/onebot", "tok"),
              std::string("ws://127.0.0.1:3011/onebot?access_token=tok"));
  CHECK_STREQ(satou::build_websocket_url("ws://127.0.0.1:3011/?foo=1", "tok"),
              std::string("ws://127.0.0.1:3011/?foo=1&access_token=tok"));

  const auto config_path = std::filesystem::path(R"(C:\bot\config.json)");
  CHECK(satou::resolve_data_path(config_path, "data/groups.json") == config_path.parent_path() / "data/groups.json");

  const auto config_file = root / "config.json";
  {
    std::ofstream output(config_file);
    output << R"({"superusers":[10001],"default_mute_seconds":120,"napcat_url":"ws://127.0.0.1:3011"})";
  }
  const auto loaded = satou::load_config(config_file);
  CHECK_EQ(loaded.superusers.size(), 1u);
  CHECK_STREQ(loaded.superusers[0], std::string("10001"));
  CHECK_EQ(loaded.default_mute_seconds, 120);

  const auto store_path = root / "groups.json";
  {
    satou::GroupStore store(store_path);
    CHECK(store.update("9", [](satou::GroupSettings& settings) { settings.words.push_back("广告"); }));
  }
  CHECK(!std::filesystem::exists(std::filesystem::path(store_path.wstring() + L".tmp")));
  satou::GroupStore reloaded(store_path);
  CHECK_EQ(reloaded.get("9").words.size(), 1u);
  CHECK_STREQ(reloaded.get("9").words[0], std::string("广告"));
  CHECK(reloaded.get("8").welcome_enabled);

  bool threw = false;
  try {
    std::ofstream broken(root / "broken.json");
    broken << "{";
    broken.close();
    satou::GroupStore ignored(root / "broken.json");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);

  satou::GroupStore defaults_store(root / "default-groups.json");
  satou::GroupSettings defaults;
  defaults.words = {"广告"};
  defaults.welcome_enabled = false;
  defaults.default_mute_seconds = 30;
  CHECK(defaults_store.set("*", defaults));
  CHECK(!defaults_store.contains("10001"));
  CHECK_EQ(defaults_store.get("10001").words.size(), 1u);
  CHECK(!defaults_store.get("10001").welcome_enabled);
  CHECK_EQ(defaults_store.get("10001").default_mute_seconds, 30);
  satou::GroupSettings custom = defaults_store.get("10001");
  custom.words.push_back("链接");
  CHECK(defaults_store.set("10001", custom));
  CHECK_EQ(defaults_store.get("10001").words.size(), 2u);
  CHECK_EQ(defaults_store.get("10002").words.size(), 1u);
  CHECK(defaults_store.remove("10001"));
  CHECK_EQ(defaults_store.get("10001").words.size(), 1u);
}

satou::ParsedMessage command_text(const std::string& text, std::vector<std::string> ats = {}) {
  satou::ParsedMessage parsed;
  parsed.text = text;
  parsed.at_qq = std::move(ats);
  return parsed;
}

void test_commands(const std::filesystem::path& root) {
  Env env(root / "commands.json");
  env.config.default_mute_seconds = 120;

  const auto muted = satou::handle_command(env.ctx, "9", "1", command_text("禁 10分", {"2"}));
  CHECK(muted.is_command);
  CHECK(muted.reply.find("已禁言") != std::string::npos);
  const auto* ban = find_call(env.api, "set_group_ban");
  CHECK(ban != nullptr);
  CHECK_EQ(ban->at("duration").get<int>(), 600);
  CHECK_EQ(ban->at("user_id").get<std::int64_t>(), std::int64_t{2});

  env.api.calls.clear();
  const auto glued = satou::handle_command(env.ctx, "9", "1", command_text("禁1234567 30秒"));
  CHECK(glued.reply.find("已禁言") != std::string::npos);
  CHECK_EQ(find_call(env.api, "set_group_ban")->at("duration").get<int>(), 30);

  env.api.calls.clear();
  const auto default_mute = satou::handle_command(env.ctx, "9", "1", command_text("禁", {"2"}));
  CHECK_EQ(find_call(env.api, "set_group_ban")->at("duration").get<int>(), 120);

  env.api.calls.clear();
  const auto lifted = satou::handle_command(env.ctx, "9", "1", command_text("解1234567"));
  CHECK_EQ(find_call(env.api, "set_group_ban")->at("duration").get<int>(), 0);
  CHECK(lifted.reply.find("解除") != std::string::npos);

  env.api.calls.clear();
  const auto member = satou::handle_command(env.ctx, "9", "2", command_text("禁", {"1"}));
  CHECK(member.reply.find("没有管理权限") != std::string::npos);
  CHECK(find_call(env.api, "set_group_ban") == nullptr);

  env.api.calls.clear();
  const auto peer = satou::handle_command(env.ctx, "9", "1", command_text("踢", {"8"}));
  CHECK(peer.reply.find("不能处罚") != std::string::npos);
  CHECK(find_call(env.api, "set_group_kick") == nullptr);

  env.api.roles["900"] = "admin";
  env.api.calls.clear();
  const auto owner_limited = satou::handle_command(env.ctx, "9", "3", command_text("黑", {"8"}));
  CHECK(owner_limited.reply.find("不能处罚") != std::string::npos);

  env.api.roles["900"] = "owner";
  env.api.calls.clear();
  const auto kicked = satou::handle_command(env.ctx, "9", "3", command_text("黑", {"8"}));
  CHECK(kicked.reply.find("拒绝") != std::string::npos);
  CHECK(find_call(env.api, "set_group_kick")->at("reject_add_request").get<bool>());

  env.api.calls.clear();
  const auto whole = satou::handle_command(env.ctx, "9", "3", command_text("全禁"));
  CHECK(find_call(env.api, "set_group_whole_ban")->at("enable").get<bool>());
  const auto unwhole = satou::handle_command(env.ctx, "9", "3", command_text("解全禁"));
  CHECK(!env.api.calls.back().second.at("enable").get<bool>());
  CHECK(whole.reply.find("全体禁言") != std::string::npos);
  CHECK(unwhole.is_command);

  env.api.calls.clear();
  const auto renamed = satou::handle_command(env.ctx, "9", "3", command_text("改 新名片", {"2"}));
  CHECK_STREQ(find_call(env.api, "set_group_card")->at("card").get<std::string>(), std::string("新名片"));
  const auto cleared = satou::handle_command(env.ctx, "9", "1", command_text("改", {"1"}));
  CHECK(cleared.reply.find("清除") != std::string::npos);

  env.api.roles["900"] = "admin";
  env.api.calls.clear();
  env.api.quoted_user = "2";
  auto recall = command_text("撤回");
  recall.reply_id = "99";
  const auto recalled = satou::handle_command(env.ctx, "9", "1", recall);
  CHECK(recalled.reply.find("已撤回") != std::string::npos);
  CHECK(find_call(env.api, "delete_msg") != nullptr);

  env.api.calls.clear();
  env.api.quoted_user = "3";
  const auto blocked = satou::handle_command(env.ctx, "9", "1", recall);
  CHECK(blocked.reply.find("不能处罚") != std::string::npos);
  CHECK(find_call(env.api, "delete_msg") == nullptr);

  env.api.calls.clear();
  const auto missing_quote = satou::handle_command(env.ctx, "9", "1", command_text("撤回"));
  CHECK(missing_quote.reply.find("引用") != std::string::npos);

  env.api.fail_actions["set_group_ban"] = 1;
  const auto failed = satou::handle_command(env.ctx, "9", "1", command_text("禁", {"2"}));
  CHECK(failed.reply.find("操作失败") != std::string::npos);
  CHECK(failed.reply.find("权限不足") != std::string::npos);
  env.api.fail_actions.clear();

  const auto not_command = satou::handle_command(env.ctx, "9", "2", command_text("禁止吸烟"));
  CHECK(!not_command.is_command);
  const auto welcome_chat = satou::handle_command(env.ctx, "9", "1", command_text("欢迎大家"));
  CHECK(!welcome_chat.is_command);

  const auto help = satou::handle_command(env.ctx, "9", "2", command_text("/帮助"));
  CHECK(help.reply.find("你不是管理员") != std::string::npos);
  const auto admin_help = satou::handle_command(env.ctx, "9", "1", command_text("帮助"));
  CHECK(admin_help.reply.find("Satou QQBot") != std::string::npos);

  const auto welcome = satou::handle_command(env.ctx, "9", "1", command_text("欢迎 关"));
  CHECK(welcome.reply.find("关闭") != std::string::npos);
  satou::GroupStore reloaded(root / "commands.json");
  CHECK(!reloaded.get("9").welcome_enabled);

  env.config.superusers.push_back("7");
  env.api.calls.clear();
  const auto super_ban = satou::handle_command(env.ctx, "9", "7", command_text("禁", {"2"}));
  CHECK(super_ban.reply.find("已禁言") != std::string::npos);
  bool looked_up_super = false;
  for (const auto& call : env.api.calls) {
    if (call.first == "get_group_member_info" && satou::json_id(call.second.at("user_id")) == "7") looked_up_super = true;
  }
  CHECK(!looked_up_super);
}

void test_events(const std::filesystem::path& root) {
  Env env(root / "events.json");
  auto message = nlohmann::json{
      {"post_type", "message"},
      {"message_type", "group"},
      {"group_id", "9"},
      {"user_id", "1"},
      {"message_id", 42},
      {"message", text_message("禁 10分")},
  };
  message["message"] = nlohmann::json::array({
      {{"type", "text"}, {"data", {{"text", "禁 "}}}},
      {{"type", "at"}, {"data", {{"qq", "2"}}}},
      {{"type", "text"}, {"data", {{"text", " 10分"}}}},
  });
  satou::handle_event(env.ctx, message);
  const auto sent = replies(env.api);
  CHECK(!sent.empty());
  CHECK(sent[0].find("已禁言") != std::string::npos);

  env.api.calls.clear();
  message["user_id"] = "900";
  satou::handle_event(env.ctx, message);
  CHECK(env.api.calls.empty());

  env.api.calls.clear();
  CHECK(env.store.update("9", [](satou::GroupSettings& settings) { settings.words.push_back("广告"); }));
  nlohmann::json spammy{
      {"post_type", "message"},
      {"message_type", "group"},
      {"group_id", "9"},
      {"user_id", "2"},
      {"message_id", 7},
      {"message", text_message("这是广告")},
  };
  satou::handle_event(env.ctx, spammy);
  CHECK(find_call(env.api, "delete_msg") != nullptr);
  CHECK_EQ(find_call(env.api, "set_group_ban")->at("duration").get<int>(), 600);
  CHECK(replies(env.api)[0].find("违禁") != std::string::npos);

  env.api.calls.clear();
  spammy["user_id"] = "1";
  satou::handle_event(env.ctx, spammy);
  CHECK(find_call(env.api, "delete_msg") == nullptr);

  env.api.calls.clear();
  CHECK(env.store.update("9", [](satou::GroupSettings& settings) { settings.spam_enabled = true; }));
  nlohmann::json chatter = spammy;
  chatter["user_id"] = "2";
  chatter["message"] = text_message("hello");
  for (int i = 0; i < 5; ++i) satou::handle_event(env.ctx, chatter);
  CHECK_EQ(count_call(env.api, "set_group_ban"), 1);
  CHECK(replies(env.api).back().find("刷屏") != std::string::npos);

  env.api.calls.clear();
  satou::handle_event(env.ctx, {{"post_type", "notice"}, {"notice_type", "group_increase"}, {"group_id", "9"}, {"user_id", "2"}});
  CHECK(replies(env.api)[0].find("欢迎 昵称2") != std::string::npos);

  env.api.calls.clear();
  CHECK(env.store.update("9", [](satou::GroupSettings& settings) { settings.welcome_enabled = false; }));
  satou::handle_event(env.ctx, {{"post_type", "notice"}, {"notice_type", "group_increase"}, {"group_id", "9"}, {"user_id", "2"}});
  CHECK(replies(env.api).empty());

  satou::handle_event(env.ctx,
                      {{"post_type", "notice"}, {"notice_type", "group_decrease"}, {"sub_type", "leave"}, {"group_id", "9"}, {"user_id", "2"}});
  CHECK(replies(env.api)[0].find("退出了群聊") != std::string::npos);
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "satou-qqbot-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  test_join_approval();
  test_duration();
  test_permission();
  test_message();
  test_words_and_spam();
  test_config_and_store(root);
  test_commands(root);
  test_events(root);
  if (g_failed != 0) {
    std::cerr << g_failed << " failed\n";
    return 1;
  }
  std::cout << "passed\n";
  return 0;
}
