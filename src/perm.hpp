#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace satou {

enum class Role { Member = 0, Admin = 1, Owner = 2, Superuser = 3 };

int role_rank(Role role);
Role role_from_group(std::string_view role);
bool is_superuser(const std::vector<std::string>& superusers, std::string_view user_id);
bool can_admin(Role role);
bool can_punish(Role actor, Role target, Role bot);

}  // namespace satou
