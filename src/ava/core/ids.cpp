#include "ava/core/ids.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>

namespace ava::core {

std::string make_id(std::string_view prefix) {
  const auto now = std::chrono::system_clock::now().time_since_epoch().count();
  std::uint64_t seed = static_cast<std::uint64_t>(now);
  try {
    std::random_device device;
    seed ^= static_cast<std::uint64_t>(device()) << 32;
    seed ^= static_cast<std::uint64_t>(device());
  } catch (...) {
    seed ^= std::hash<std::thread::id>{}(std::this_thread::get_id());
  }
  std::mt19937_64 generator(seed);
  const auto random = generator();

  std::ostringstream out;
  out << prefix << '_';
  out << std::hex << static_cast<std::uint64_t>(now) << '_';
  out << std::setw(16) << std::setfill('0') << random;
  return out.str();
}

}  // namespace ava::core
