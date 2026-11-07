#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace satou {

struct GroupSettings {
  bool welcome_enabled = true;
  std::string welcome_text = "欢迎 {nickname}（{user_id}）";
  bool leave_enabled = true;
  std::vector<std::string> words;
  int word_mute_seconds = 600;
  bool spam_enabled = false;
  int spam_limit = 5;
  int spam_window_seconds = 8;
  int spam_mute_seconds = 60;
  int default_mute_seconds = 600;
  bool approve_enabled = false;
  bool approve_reject_unmatched = false;
  std::vector<std::string> approve_words;
  std::vector<std::string> reject_words;
  std::vector<std::string> approve_blacklist;
  std::string approve_reject_reason = "未通过入群审批";
};

class GroupStore {
 public:
  explicit GroupStore(std::filesystem::path path);

  GroupSettings get(const std::string& group_id) const;
  bool contains(const std::string& group_id) const;
  std::vector<std::string> ids() const;
  bool update(const std::string& group_id, const std::function<void(GroupSettings&)>& edit);
  bool set(const std::string& group_id, const GroupSettings& settings);
  bool remove(const std::string& group_id);

 private:
  GroupSettings lookup_unlocked(const std::string& group_id) const;
  bool load();
  bool save_unlocked() const;

  std::filesystem::path path_;
  std::map<std::string, GroupSettings> groups_;
  mutable std::mutex mu_;
};

}  // namespace satou
