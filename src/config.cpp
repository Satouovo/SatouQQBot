#include "config.hpp"

#include "util.hpp"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace satou {
namespace {

std::string url_encode(const std::string& text) {
  std::ostringstream out;
  for (unsigned char ch : text) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out << static_cast<char>(ch);
    } else {
      out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

}  // namespace

std::string build_websocket_url(const std::string& napcat_url, const std::string& access_token) {
  if (access_token.empty()) return napcat_url;
  std::string url = napcat_url;
  const std::string encoded = url_encode(access_token);
  if (url.find('?') != std::string::npos) {
    url += "&access_token=" + encoded;
    return url;
  }
  const auto scheme = url.find("://");
  const auto slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  if (slash == std::string::npos) url += '/';
  url += "?access_token=" + encoded;
  return url;
}

std::filesystem::path resolve_data_path(const std::filesystem::path& config_path, const std::string& data_file) {
  const std::filesystem::path data(data_file);
  if (data.is_absolute()) return data;
  return config_path.parent_path() / data;
}

void save_config(const std::filesystem::path& path, const AppConfig& config) {
  const nlohmann::json json{{"napcat_url", config.napcat_url},
                            {"access_token", config.access_token},
                            {"superusers", config.superusers},
                            {"command_prefixes", config.command_prefixes},
                            {"default_mute_seconds", config.default_mute_seconds},
                            {"data_file", config.data_file}};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("无法保存配置：" + path.string());
  const std::string text = json.dump(2) + '\n';
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.flush();
  if (!output.good()) throw std::runtime_error("无法保存配置：" + path.string());
}

AppConfig load_config(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("找不到配置文件 " + path.string() + "，请把 config.example.json 复制为 config.json");
  }
  nlohmann::json json;
  try {
    input >> json;
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("配置文件不是合法 JSON：") + ex.what());
  }
  if (!json.is_object()) throw std::runtime_error("配置文件必须是 JSON 对象");

  AppConfig config;
  if (json.contains("napcat_url")) config.napcat_url = json.at("napcat_url").get<std::string>();
  if (json.contains("access_token")) config.access_token = json.at("access_token").get<std::string>();
  if (json.contains("superusers")) {
    config.superusers.clear();
    for (const auto& user : json.at("superusers")) config.superusers.push_back(json_id(user));
  }
  if (json.contains("command_prefixes")) {
    config.command_prefixes.clear();
    for (const auto& prefix : json.at("command_prefixes")) config.command_prefixes.push_back(prefix.get<std::string>());
  }
  if (json.contains("default_mute_seconds")) config.default_mute_seconds = json.at("default_mute_seconds").get<int>();
  if (json.contains("data_file")) config.data_file = json.at("data_file").get<std::string>();
  if (config.napcat_url.empty()) throw std::runtime_error("napcat_url 不能为空");
  if (config.default_mute_seconds <= 0 || config.default_mute_seconds > 30 * 24 * 60 * 60) {
    throw std::runtime_error("default_mute_seconds 必须在 1 到 2592000 之间");
  }
  return config;
}

}  // namespace satou
