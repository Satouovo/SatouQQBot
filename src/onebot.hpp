#pragma once

#include "perm.hpp"

#include <easywsclient/easywsclient.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace satou {

class OneBotApi {
 public:
  virtual ~OneBotApi() = default;
  virtual nlohmann::json call(const std::string& action, const nlohmann::json& params) = 0;
};

struct ApiResult {
  bool ok = false;
  nlohmann::json data = nullptr;
  std::string message;
  int retcode = -1;
};

struct MemberProfile {
  bool ok = false;
  Role role = Role::Member;
  std::string nickname;
  std::string card;
  std::string error;
};

ApiResult interpret(const nlohmann::json& response);
MemberProfile get_member(OneBotApi& api, const std::string& group_id, const std::string& user_id);
bool send_group_text(OneBotApi& api, const std::string& group_id, const std::string& text);

class NapCatClient : public OneBotApi {
 public:
  explicit NapCatClient(std::string url);
  ~NapCatClient() override;
  NapCatClient(const NapCatClient&) = delete;
  NapCatClient& operator=(const NapCatClient&) = delete;

  bool connect();
  void close();
  bool connected() const;
  nlohmann::json call(const std::string& action, const nlohmann::json& params) override;
  std::vector<nlohmann::json> poll_events(int timeout_ms);
  std::vector<nlohmann::json> take_events();

 private:
  void ensure_wsa();
  void pump(int timeout_ms);
  void ingest(const std::string& message);

  std::string url_;
  easywsclient::WebSocket::pointer socket_ = nullptr;
  std::vector<nlohmann::json> events_;
  std::optional<nlohmann::json> pending_;
  std::string waiting_echo_;
  bool waiting_ = false;
  std::uint64_t echo_seq_ = 1;
  bool wsa_ready_ = false;
};

}  // namespace satou
