#include "ava/types/time.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ava::types {

std::string now_utc_rfc3339() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif

  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace ava::types
