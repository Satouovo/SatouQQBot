#include "onebot.hpp"

#include "util.hpp"

#ifdef _WIN32
#include <WinSock2.h>
#endif

#include <chrono>
#include <stdexcept>

namespace satou {
namespace {

nlohmann::json failed(const std::string& message) {
  return nlohmann::json{{"status", "failed"}, {"retcode", -1}, {"data", nullptr}, {"message", message}};
}

}  // namespace

ApiResult interpret(const nlohmann::json& response) {
  ApiResult result;
  result.retcode = response.value("retcode", -1);
  if (response.contains("message") && response.at("message").is_string()) result.message = response.at("message").get<std::string>();
  if (response.contains("data")) result.data = response.at("data");
  const auto status = response.value("status", "");
  result.ok = status == "ok" || result.retcode == 0;
  if (!result.ok && result.message.empty() && response.contains("wording") && response.at("wording").is_string()) {
    result.message = response.at("wording").get<std::string>();
  }
  if (!result.ok && result.message.empty() && response.contains("msg") && response.at("msg").is_string()) {
    result.message = response.at("msg").get<std::string>();
  }
  if (!result.ok && result.message.empty()) result.message = "retcode " + std::to_string(result.retcode);
  return result;
}

MemberProfile get_member(OneBotApi& api, const std::string& group_id, const std::string& user_id) {
  const auto result = interpret(api.call("get_group_member_info",
                                         {{"group_id", id_json(group_id)}, {"user_id", id_json(user_id)}, {"no_cache", true}}));
  MemberProfile profile;
  if (!result.ok || !result.data.is_object()) {
    profile.error = result.message.empty() ? "没有成员资料" : result.message;
    return profile;
  }
  profile.ok = true;
  profile.role = role_from_group(result.data.value("role", "member"));
  if (result.data.contains("nickname") && result.data.at("nickname").is_string()) {
    profile.nickname = result.data.at("nickname").get<std::string>();
  }
  if (result.data.contains("card") && result.data.at("card").is_string()) profile.card = result.data.at("card").get<std::string>();
  return profile;
}

bool send_group_text(OneBotApi& api, const std::string& group_id, const std::string& text) {
  const auto result = interpret(api.call("send_group_msg", {{"group_id", id_json(group_id)}, {"message", text}}));
  if (!result.ok) log_info("发送群消息失败：" + result.message);
  return result.ok;
}

NapCatClient::NapCatClient(std::string url) : url_(std::move(url)) {}

NapCatClient::~NapCatClient() { close(); }

void NapCatClient::ensure_wsa() {
#ifdef _WIN32
  if (wsa_ready_) return;
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup 失败");
  wsa_ready_ = true;
#else
  wsa_ready_ = true;
#endif
}

bool NapCatClient::connect() {
  ensure_wsa();
  close();
  socket_ = easywsclient::WebSocket::from_url(url_);
  return connected();
}

void NapCatClient::close() {
  if (!socket_) return;
  if (socket_->getReadyState() != easywsclient::WebSocket::CLOSED) {
    socket_->close();
    for (int attempt = 0; attempt < 5 && socket_->getReadyState() != easywsclient::WebSocket::CLOSED; ++attempt) {
      socket_->poll(20);
    }
  }
  delete socket_;
  socket_ = nullptr;
}

bool NapCatClient::connected() const {
  return socket_ && socket_->getReadyState() == easywsclient::WebSocket::OPEN;
}

void NapCatClient::ingest(const std::string& message) {
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(message);
  } catch (const std::exception&) {
    log_info("忽略无法解析的 WebSocket 消息");
    return;
  }
  if (waiting_ && json.contains("echo") && !json.contains("post_type") && echo_text(json.at("echo")) == waiting_echo_) {
    pending_ = std::move(json);
    return;
  }
  if (json.contains("post_type")) events_.push_back(std::move(json));
}

void NapCatClient::pump(int timeout_ms) {
  if (!socket_) return;
  socket_->poll(timeout_ms);
  socket_->dispatch([this](const std::string& message) { ingest(message); });
}

nlohmann::json NapCatClient::call(const std::string& action, const nlohmann::json& params) {
  if (!connected()) return failed("未连接到 NapCat");
  if (waiting_) return failed("内部错误：重复调用接口");

  const std::string echo = std::to_string(++echo_seq_);
  const nlohmann::json request{{"action", action}, {"params", params.is_object() ? params : nlohmann::json::object()}, {"echo", echo}};
  waiting_ = true;
  waiting_echo_ = echo;
  pending_.reset();
  socket_->send(request.dump());

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (!pending_ && connected() && std::chrono::steady_clock::now() < deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    const int slice = left < 50 ? static_cast<int>(left < 0 ? 0 : left) : 50;
    pump(slice);
  }
  waiting_ = false;
  if (!pending_) return failed(connected() ? "接口超时" : "连接中断");
  auto response = std::move(*pending_);
  pending_.reset();
  return response;
}

std::vector<nlohmann::json> NapCatClient::poll_events(int timeout_ms) {
  pump(timeout_ms);
  return take_events();
}

std::vector<nlohmann::json> NapCatClient::take_events() {
  std::vector<nlohmann::json> ready;
  ready.swap(events_);
  return ready;
}

}  // namespace satou
