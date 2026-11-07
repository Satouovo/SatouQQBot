#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace satou {

struct AppConfig {
  std::string napcat_url = "ws://127.0.0.1:3011";
  std::string access_token;
  std::vector<std::string> superusers;
  std::vector<std::string> command_prefixes{"", "/"};
  int default_mute_seconds = 600;
  std::string data_file = "data/groups.json";
};

AppConfig load_config(const std::filesystem::path& path);
void save_config(const std::filesystem::path& path, const AppConfig& config);
std::string build_websocket_url(const std::string& napcat_url, const std::string& access_token);
std::filesystem::path resolve_data_path(const std::filesystem::path& config_path, const std::string& data_file);

}  // namespace satou
