#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "ava/platform/platform.hpp"

namespace {

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_platform_test_" + unique);
}

}  // namespace

TEST_CASE("standard platform performs basic file operations", "[ava_platform]") {
  ava::platform::StandardPlatform platform;

  const auto root = temp_root_for_test();
  const auto nested_dir = root / "a" / "b";
  const auto file_path = nested_dir / "hello.txt";

  REQUIRE_FALSE(platform.exists(file_path));

  platform.create_dir_all(nested_dir);
  REQUIRE(platform.exists(nested_dir));
  REQUIRE(platform.is_directory(nested_dir));

  platform.write_file(file_path, "hello milestone 3");
  REQUIRE(platform.exists(file_path));
  REQUIRE_FALSE(platform.is_directory(file_path));
  REQUIRE(platform.read_file(file_path) == "hello milestone 3");

  const auto info = platform.metadata(file_path);
  REQUIRE(info.path == file_path);
  REQUIRE(info.size > 0);
  REQUIRE_FALSE(info.is_directory);

  std::filesystem::remove_all(root);
}

TEST_CASE("command execution remains intentionally deferred", "[ava_platform]") {
  REQUIRE_FALSE(ava::platform::StandardPlatform::supports_command_execution());
}
