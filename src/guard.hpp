#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace satou {

struct BotContext;

class SpamTracker {
 public:
  bool observe(const std::string& group_id, const std::string& user_id, int limit, int window_seconds,
               std::chrono::steady_clock::time_point now);
  void clear(const std::string& group_id, const std::string& user_id);

 private:
  std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> hits_;
};

std::optional<std::string> find_banned_word(std::string_view text, const std::vector<std::string>& words);
void handle_notice(BotContext& ctx, const nlohmann::json& event);
void handle_group_request(BotContext& ctx, const nlohmann::json& event);
bool handle_automod(BotContext& ctx, const std::string& group_id, const std::string& user_id, const std::string& text,
                    const nlohmann::json& message_id);

}  // namespace satou
