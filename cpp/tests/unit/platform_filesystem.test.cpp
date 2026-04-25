#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "ava/platform/filesystem.hpp"
#include "ava/platform/platform.hpp"

namespace {

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_platform_test_" + unique);
}

struct TempDirGuard {
  std::filesystem::path path;

  explicit TempDirGuard(std::filesystem::path root) : path(std::move(root)) {}

  ~TempDirGuard() { std::filesystem::remove_all(path); }

  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

}  // namespace

TEST_CASE("standard platform performs basic file operations", "[ava_platform]") {
  ava::platform::StandardPlatform platform;

  const auto root = temp_root_for_test();
  const TempDirGuard cleanup(root);
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
}

TEST_CASE("local filesystem directly performs basic file operations", "[ava_platform]") {
  ava::platform::LocalFileSystem filesystem;

  const auto root = temp_root_for_test();
  const TempDirGuard cleanup(root);
  const auto nested_dir = root / "direct";
  const auto file_path = nested_dir / "hello.txt";

  filesystem.write_file(file_path, "direct local fs");

  REQUIRE(filesystem.exists(file_path));
  REQUIRE_FALSE(filesystem.is_directory(file_path));
  REQUIRE(filesystem.read_file(file_path) == "direct local fs");

  const auto dir_info = filesystem.metadata(nested_dir);
  REQUIRE(dir_info.path == nested_dir);
  REQUIRE(dir_info.is_directory);

  const auto file_info = filesystem.metadata(file_path);
  REQUIRE(file_info.path == file_path);
  REQUIRE(file_info.size == std::string{"direct local fs"}.size());
  REQUIRE_FALSE(file_info.is_directory);
}

TEST_CASE("local filesystem reports missing and invalid paths as errors", "[ava_platform]") {
  ava::platform::LocalFileSystem filesystem;

  const auto root = temp_root_for_test();
  const TempDirGuard cleanup(root);
  const auto missing = root / "missing.txt";

  REQUIRE_THROWS_AS(filesystem.read_file(missing), std::runtime_error);
  REQUIRE_THROWS_AS(filesystem.metadata(missing), std::runtime_error);

  const auto file_as_parent = root / "parent-file";
  filesystem.write_file(file_as_parent, "not a directory");
  REQUIRE_THROWS_AS(filesystem.write_file(file_as_parent / "child.txt", "cannot create"), std::runtime_error);
}

TEST_CASE("command execution remains intentionally deferred", "[ava_platform]") {
  REQUIRE_FALSE(ava::platform::StandardPlatform::supports_command_execution());
}
