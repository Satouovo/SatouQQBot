#include "perm.hpp"

namespace satou {

int role_rank(Role role) { return static_cast<int>(role); }

Role role_from_group(std::string_view role) {
  if (role == "owner") return Role::Owner;
  if (role == "admin") return Role::Admin;
  return Role::Member;
}

bool is_superuser(const std::vector<std::string>& superusers, std::string_view user_id) {
  for (const auto& id : superusers) {
    if (id == user_id) return true;
  }
  return false;
}

bool can_admin(Role role) { return role_rank(role) >= role_rank(Role::Admin); }

bool can_punish(Role actor, Role target, Role bot) {
  if (!can_admin(actor)) return false;
  if (role_rank(target) >= role_rank(actor)) return false;
  if (role_rank(target) >= role_rank(bot)) return false;
  return true;
}

}  // namespace satou
