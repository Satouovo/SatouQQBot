#include "runner.hpp"

#include "context.hpp"
#include "dispatch.hpp"
#include "guard.hpp"
#include "onebot.hpp"
#include "store.hpp"
#include "util.hpp"

#include <chrono>
#include <deque>
#include <thread>

namespace satou {
namespace {

void sleep_for_stop(const std::atomic<bool>& stop, std::chrono::milliseconds total) {
  const auto deadline = std::chrono::steady_clock::now() + total;
  while (!stop.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace

void run_bot(const AppConfig& config, GroupStore& store, const std::atomic<bool>& stop,
             const std::function<void(const std::string&)>& status) {
  SpamTracker spam;
  NapCatClient client(build_websocket_url(config.napcat_url, config.access_token));
  BotContext ctx{config, store, spam, client, {}};
  log_info("Satou QQBot 启动，NapCat " + config.napcat_url);
  if (status) status("正在连接");

  while (!stop.load()) {
    if (!client.connected()) {
      log_info("正在连接 NapCat");
      if (status) status("正在连接");
      if (!client.connect()) {
        log_info("连接失败，3 秒后重试");
        if (status) status("连接失败，正在重试");
        sleep_for_stop(stop, std::chrono::seconds(3));
        continue;
      }
      const auto info = interpret(client.call("get_login_info", nlohmann::json::object()));
      if (!info.ok || !info.data.is_object() || !info.data.contains("user_id")) {
        log_info("没有取得登录账号：" + info.message);
        if (status) status("登录失败，正在重试");
        client.close();
        sleep_for_stop(stop, std::chrono::seconds(3));
        continue;
      }
      ctx.self_id = json_id(info.data.at("user_id"));
      log_info("已登录 " + ctx.self_id);
      if (status) status("已登录 " + ctx.self_id);
    }

    std::deque<nlohmann::json> queue;
    for (auto& event : client.poll_events(200)) queue.push_back(std::move(event));
    if (stop.load()) break;
    if (!client.connected()) {
      log_info("与 NapCat 的连接已断开");
      if (status) status("连接已断开，正在重试");
      continue;
    }
    while (!queue.empty() && !stop.load()) {
      auto event = std::move(queue.front());
      queue.pop_front();
      try {
        handle_event(ctx, event);
      } catch (const std::exception& ex) {
        log_info(std::string("处理事件失败：") + ex.what());
      }
      for (auto& extra : client.take_events()) queue.push_back(std::move(extra));
    }
  }
  client.close();
  log_info("Satou QQBot 已停止");
}

}  // namespace satou
