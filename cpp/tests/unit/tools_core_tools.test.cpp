#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <nlohmann/json.hpp>

#include "ava/tools/core_tools.hpp"

namespace {

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_tools_test_" + unique);
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

class CwdGuard {
 public:
  CwdGuard() : previous_(std::filesystem::current_path()) {}
  ~CwdGuard() { std::filesystem::current_path(previous_); }

 private:
  std::filesystem::path previous_;
};

}  // namespace

TEST_CASE("default tools registration includes milestone 6 core set", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::ToolRegistry registry;
  const auto registration = ava::tools::register_default_tools(registry, root);

  REQUIRE(registration.backup_session != nullptr);
  REQUIRE(registry.has_tool("read"));
  REQUIRE(registry.has_tool("write"));
  REQUIRE(registry.has_tool("edit"));
  REQUIRE(registry.has_tool("bash"));
  REQUIRE(registry.has_tool("glob"));
  REQUIRE(registry.has_tool("grep"));
  REQUIRE(registry.has_tool("git"));
  REQUIRE(registry.has_tool("git_read"));
  REQUIRE_FALSE(registry.has_tool("web_fetch"));
  REQUIRE_FALSE(registry.has_tool("web_search"));

  std::filesystem::remove_all(root);
}

TEST_CASE("read/write/edit operate within workspace", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::ReadTool read_tool(root);
  ava::tools::EditTool edit_tool(root, backup);

  const auto file = root / "nested" / "a.txt";
  write_tool.execute(nlohmann::json{{"path", "nested/a.txt"}, {"content", "hello\nworld\n"}});

  const auto read_result = read_tool.execute(nlohmann::json{{"path", "nested/a.txt"}});
  REQUIRE(read_result.content.find("1: hello") != std::string::npos);
  REQUIRE(read_result.content.find("2: world") != std::string::npos);

  const auto edit_result = edit_tool.execute(
      nlohmann::json{{"path", "nested/a.txt"}, {"old_text", "world"}, {"new_text", "ava"}}
  );
  REQUIRE(edit_result.content.find("exact_match") != std::string::npos);
  REQUIRE(read_text_file(file) == "hello\nava\n");

  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all updates all occurrences", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  write_tool.execute(nlohmann::json{{"path", "many.txt"}, {"content", "x\nx\nx\n"}});

  const auto result = edit_tool.execute(nlohmann::json{{"path", "many.txt"},
                                                        {"old_text", "x"},
                                                        {"new_text", "y"},
                                                        {"replace_all", true}});

  REQUIRE(result.content.find("replace_all") != std::string::npos);
  REQUIRE(read_text_file(root / "many.txt") == "y\ny\ny\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit rejects empty old_text", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  write_tool.execute(nlohmann::json{{"path", "empty.txt"}, {"content", "abc"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "empty.txt"},
                                                  {"old_text", ""},
                                                  {"new_text", "x"},
                                                  {"replace_all", true}}));
  std::filesystem::remove_all(root);
}

TEST_CASE("glob and grep find files and content", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root / "src");

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::GlobTool glob_tool(root);
  ava::tools::GrepTool grep_tool(root);

  write_tool.execute(nlohmann::json{{"path", "src/a.rs"}, {"content", "let status = 1;\n"}});
  write_tool.execute(nlohmann::json{{"path", "src/b.txt"}, {"content", "status: ok\n"}});

  const auto glob_result =
      glob_tool.execute(nlohmann::json{{"pattern", "**/*.rs"}, {"path", "src"}}).content;
  REQUIRE(glob_result.find("a.rs") != std::string::npos);
  REQUIRE(glob_result.find("b.txt") == std::string::npos);

  const auto grep_result = grep_tool
                               .execute(nlohmann::json{{"pattern", "status"}, {"path", "src"}, {"include", "*.rs"}})
                               .content;
  REQUIRE(grep_result.find("a.rs:1") != std::string::npos);
  REQUIRE(grep_result.find("b.txt") == std::string::npos);

  std::filesystem::remove_all(root);
}

TEST_CASE("bash executes command with captured output", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::BashTool bash_tool(root);
  const auto result = bash_tool.execute(nlohmann::json{{"command", "printf 'hello'"}});

  REQUIRE(result.content.find("hello") != std::string::npos);
  REQUIRE(result.content.find("exit_code: 0") != std::string::npos);
  std::filesystem::remove_all(root);
}

TEST_CASE("git and git_read execute read-only git commands", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  CwdGuard cwd_guard;
  std::filesystem::current_path(root);
  std::system("git init >/dev/null 2>&1");

  ava::tools::GitReadTool git_tool(root);
  const auto status_result = git_tool.execute(nlohmann::json{{"command", "status --short"}});
  REQUIRE(status_result.content.find("exit_code: 0") != std::string::npos);

  ava::tools::GitReadAliasTool git_read_tool(root);
  const auto log_result = git_read_tool.execute(nlohmann::json{{"command", "status --short"}});
  REQUIRE(log_result.content.find("exit_code: 0") != std::string::npos);

  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "push origin main"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "branch -D main"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "tag -f v1.0"}}));

  std::filesystem::remove_all(root);
}

TEST_CASE("glob matches root-level files for doublestar patterns", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::GlobTool glob_tool(root);

  write_tool.execute(nlohmann::json{{"path", "root.rs"}, {"content", "fn main() {}\n"}});
  const auto result = glob_tool.execute(nlohmann::json{{"pattern", "**/*.rs"}, {"path", "."}}).content;
  REQUIRE(result.find("root.rs") != std::string::npos);

  std::filesystem::remove_all(root);
}

TEST_CASE("grep reports invalid regex as tool error", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::GrepTool grep_tool(root);
  const auto result = grep_tool.execute(nlohmann::json{{"pattern", "("}, {"path", "."}});
  REQUIRE(result.is_error);
  REQUIRE(result.content.find("Invalid regex") != std::string::npos);

  std::filesystem::remove_all(root);
}
