#include "store.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace satou {
namespace {

nlohmann::json settings_to_json(const GroupSettings& settings) {
  return nlohmann::json{
      {"welcome_enabled", settings.welcome_enabled},
      {"welcome_text", settings.welcome_text},
      {"leave_enabled", settings.leave_enabled},
      {"words", settings.words},
      {"word_mute_seconds", settings.word_mute_seconds},
      {"spam_enabled", settings.spam_enabled},
      {"spam_limit", settings.spam_limit},
      {"spam_window_seconds", settings.spam_window_seconds},
      {"spam_mute_seconds", settings.spam_mute_seconds},
      {"default_mute_seconds", settings.default_mute_seconds},
      {"approve_enabled", settings.approve_enabled},
      {"approve_reject_unmatched", settings.approve_reject_unmatched},
      {"approve_words", settings.approve_words},
      {"reject_words", settings.reject_words},
      {"approve_blacklist", settings.approve_blacklist},
      {"approve_reject_reason", settings.approve_reject_reason},
  };
}

GroupSettings settings_from_json(const nlohmann::json& json) {
  GroupSettings settings;
  if (json.contains("welcome_enabled")) settings.welcome_enabled = json.at("welcome_enabled").get<bool>();
  if (json.contains("welcome_text")) settings.welcome_text = json.at("welcome_text").get<std::string>();
  if (json.contains("leave_enabled")) settings.leave_enabled = json.at("leave_enabled").get<bool>();
  if (json.contains("words") && json.at("words").is_array()) {
    for (const auto& word : json.at("words")) {
      if (word.is_string() && !word.get<std::string>().empty()) settings.words.push_back(word.get<std::string>());
    }
  }
  if (json.contains("word_mute_seconds")) settings.word_mute_seconds = json.at("word_mute_seconds").get<int>();
  if (json.contains("spam_enabled")) settings.spam_enabled = json.at("spam_enabled").get<bool>();
  if (json.contains("spam_limit")) settings.spam_limit = json.at("spam_limit").get<int>();
  if (json.contains("spam_window_seconds")) settings.spam_window_seconds = json.at("spam_window_seconds").get<int>();
  if (json.contains("spam_mute_seconds")) settings.spam_mute_seconds = json.at("spam_mute_seconds").get<int>();
  if (json.contains("default_mute_seconds")) settings.default_mute_seconds = json.at("default_mute_seconds").get<int>();
  if (json.contains("approve_enabled")) settings.approve_enabled = json.at("approve_enabled").get<bool>();
  if (json.contains("approve_reject_unmatched")) {
    settings.approve_reject_unmatched = json.at("approve_reject_unmatched").get<bool>();
  }
  auto read_words = [&](const char* key, std::vector<std::string>& out) {
    if (!json.contains(key) || !json.at(key).is_array()) return;
    for (const auto& word : json.at(key)) {
      if (word.is_string() && !word.get<std::string>().empty()) out.push_back(word.get<std::string>());
    }
  };
  read_words("approve_words", settings.approve_words);
  read_words("reject_words", settings.reject_words);
  read_words("approve_blacklist", settings.approve_blacklist);
  if (json.contains("approve_reject_reason") && json.at("approve_reject_reason").is_string()) {
    settings.approve_reject_reason = json.at("approve_reject_reason").get<std::string>();
  }
  return settings;
}

bool atomic_write(const std::filesystem::path& path, const std::string& bytes) {
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output.good()) return false;
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
  return true;
}

}  // namespace

GroupStore::GroupStore(std::filesystem::path path) : path_(std::move(path)) {
  std::error_code error;
  if (std::filesystem::exists(path_, error)) {
    if (!load()) throw std::runtime_error("群配置无法读取：" + path_.string());
  }
}

GroupSettings GroupStore::lookup_unlocked(const std::string& group_id) const {
  const auto found = groups_.find(group_id);
  if (found != groups_.end()) return found->second;
  if (group_id != "*") {
    const auto fallback = groups_.find("*");
    if (fallback != groups_.end()) return fallback->second;
  }
  return {};
}

GroupSettings GroupStore::get(const std::string& group_id) const {
  std::lock_guard lock(mu_);
  return lookup_unlocked(group_id);
}

bool GroupStore::contains(const std::string& group_id) const {
  std::lock_guard lock(mu_);
  return groups_.find(group_id) != groups_.end();
}

std::vector<std::string> GroupStore::ids() const {
  std::lock_guard lock(mu_);
  std::vector<std::string> result;
  for (const auto& [group_id, settings] : groups_) {
    (void)settings;
    if (group_id != "*") result.push_back(group_id);
  }
  return result;
}

bool GroupStore::update(const std::string& group_id, const std::function<void(GroupSettings&)>& edit) {
  std::lock_guard lock(mu_);
  auto settings = lookup_unlocked(group_id);
  edit(settings);
  groups_[group_id] = std::move(settings);
  return save_unlocked();
}

bool GroupStore::set(const std::string& group_id, const GroupSettings& settings) {
  std::lock_guard lock(mu_);
  groups_[group_id] = settings;
  return save_unlocked();
}

bool GroupStore::remove(const std::string& group_id) {
  std::lock_guard lock(mu_);
  groups_.erase(group_id);
  return save_unlocked();
}

bool GroupStore::load() {
  std::ifstream input(path_);
  if (!input) return false;
  nlohmann::json json;
  try {
    input >> json;
  } catch (const std::exception&) {
    return false;
  }
  if (!json.is_object() || !json.contains("groups") || !json.at("groups").is_object()) return false;
  groups_.clear();
  for (const auto& [group_id, settings] : json.at("groups").items()) {
    if (settings.is_object()) groups_[group_id] = settings_from_json(settings);
  }
  return true;
}

bool GroupStore::save_unlocked() const {
  nlohmann::json groups = nlohmann::json::object();
  for (const auto& [group_id, settings] : groups_) groups[group_id] = settings_to_json(settings);
  const nlohmann::json root{{"groups", groups}};
  return atomic_write(path_, root.dump(2) + '\n');
}

}  // namespace satou
