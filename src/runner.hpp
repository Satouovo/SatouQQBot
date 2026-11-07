#pragma once

#include "config.hpp"
#include "store.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace satou {

void run_bot(const AppConfig& config, GroupStore& store, const std::atomic<bool>& stop,
             const std::function<void(const std::string&)>& status);

}  // namespace satou
