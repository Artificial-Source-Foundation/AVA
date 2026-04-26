#include "ava/config/keychain.hpp"

namespace ava::config {

std::string redact_key_for_log(std::string_view key) {
  if(key.size() <= 4U) {
    return "****";
  }
  return "****..." + std::string(key.substr(key.size() - 4U));
}

bool os_keychain_available() {
  return false;
}

}  // namespace ava::config
