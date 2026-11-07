#include "dispatch.hpp"

#include "commands.hpp"
#include "context.hpp"
#include "guard.hpp"
#include "message.hpp"
#include "util.hpp"

namespace satou {

void handle_event(BotContext& ctx, const nlohmann::json& event) {
  const auto post = event.value("post_type", "");
  if (post == "message") {
    if (event.value("message_type", "") != "group") return;
    if (!event.contains("group_id") || !event.contains("user_id") || !event.contains("message")) return;
    const auto group_id = json_id(event.at("group_id"));
    const auto user_id = json_id(event.at("user_id"));
    if (group_id.empty() || user_id.empty() || user_id == ctx.self_id) return;
    const auto parsed = parse_message(event.at("message"));
    const auto command = handle_command(ctx, group_id, user_id, parsed);
    if (command.is_command) {
      if (!command.reply.empty()) send_group_text(ctx.api, group_id, command.reply);
      return;
    }
    const nlohmann::json message_id = event.contains("message_id") ? event.at("message_id") : nlohmann::json();
    handle_automod(ctx, group_id, user_id, parsed.text, message_id);
    return;
  }
  if (post == "notice") handle_notice(ctx, event);
  if (post == "request") handle_group_request(ctx, event);
}

}  // namespace satou
