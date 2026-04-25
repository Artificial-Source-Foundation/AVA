#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/tools/core_tools.hpp"
#include "ava/tools/output_fallback.hpp"
#include "ava/tools/path_guard.hpp"

namespace {

std::filesystem::path temp_root_for_test() {
  static std::atomic<std::uint64_t> counter{0};
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("ava_cpp_tools_test_" + unique + "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

class CwdGuard {
 public:
  CwdGuard() : previous_(std::filesystem::current_path()) {}
  ~CwdGuard() { std::filesystem::current_path(previous_); }
  CwdGuard(const CwdGuard&) = delete;
  CwdGuard& operator=(const CwdGuard&) = delete;

 private:
  std::filesystem::path previous_;
};

void require_write_ok(ava::tools::WriteTool& tool, nlohmann::json args) {
  const auto result = tool.execute(args);
  REQUIRE_FALSE(result.is_error);
}

ava::types::ToolResult require_edit_ok(ava::tools::EditTool& tool, nlohmann::json args) {
  const auto result = tool.execute(args);
  REQUIRE_FALSE(result.is_error);
  return result;
}

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

TEST_CASE("output fallback truncates oversized payloads with deterministic footer", "[ava_tools]") {
  const auto content = std::string("0123456789abcdef");
  const auto truncated = ava::tools::apply_output_fallback("read", content, 8);
  const auto untouched = ava::tools::apply_output_fallback("read", content, content.size());

  REQUIRE(truncated.rfind("01234567", 0) == 0);
  REQUIRE(truncated.find("(Output truncated by read: showing first 8 bytes)") != std::string::npos);
  REQUIRE(untouched == content);
}

TEST_CASE("path guard normalizes workspace and rejects direct escapes", "[ava_tools]") {
  const auto root = temp_root_for_test();
  const auto outside = temp_root_for_test();
  std::filesystem::create_directories(root / "subdir");
  std::filesystem::create_directories(outside);

  const auto normalized = ava::tools::normalize_workspace_root(root / ".");
  REQUIRE(normalized == std::filesystem::weakly_canonical(root));
  REQUIRE(ava::tools::enforce_workspace_path(normalized, "subdir/../subdir", "test") == normalized / "subdir");
  REQUIRE_THROWS(ava::tools::enforce_workspace_path(normalized, "../escape.txt", "test"));
  REQUIRE_THROWS(ava::tools::enforce_workspace_path(normalized, (outside / "file.txt").string(), "test"));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside);
}

TEST_CASE("read/write/edit operate within workspace", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::ReadTool read_tool(root);
  ava::tools::EditTool edit_tool(root, backup);

  const auto file = root / "nested" / "a.txt";
  require_write_ok(write_tool, nlohmann::json{{"path", "nested/a.txt"}, {"content", "hello\nworld\n"}});

  const auto read_result = read_tool.execute(nlohmann::json{{"path", "nested/a.txt"}});
  REQUIRE(read_result.content.find("1: hello") != std::string::npos);
  REQUIRE(read_result.content.find("2: world") != std::string::npos);

  const auto limited_read = read_tool.execute(nlohmann::json{{"path", "nested/a.txt"}, {"offset", 2}, {"limit", 1}});
  REQUIRE(limited_read.content == "2: world");

  const auto edit_result = edit_tool.execute(
      nlohmann::json{{"path", "nested/a.txt"}, {"old_text", "world"}, {"new_text", "ava"}}
  );
  REQUIRE(edit_result.content.find("exact_match") != std::string::npos);
  REQUIRE(read_text_file(file) == "hello\nava\n");

  std::filesystem::remove_all(root);
}

TEST_CASE("read rejects oversized regular files before loading", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  const auto path = root / "large-read.txt";
  std::ofstream out(path, std::ios::binary);
  out << std::string(8 * 1024 * 1024 + 1, 'x');
  out.close();

  ava::tools::ReadTool read_tool(root);
  REQUIRE_THROWS(read_tool.execute(nlohmann::json{{"path", "large-read.txt"}}));
  std::filesystem::remove_all(root);
}

TEST_CASE("read applies output fallback to large file output", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::ReadTool read_tool(root);

  std::ostringstream content;
  for(std::size_t line = 0; line < 2000; ++line) {
    content << std::string(80, 'x') << "\n";
  }
  require_write_ok(write_tool, nlohmann::json{{"path", "large-read.txt"}, {"content", content.str()}});

  const auto result = read_tool.execute(nlohmann::json{{"path", "large-read.txt"}});
  REQUIRE(result.content.find("Output truncated by read") != std::string::npos);
  REQUIRE(result.content.size() < 50000);

  std::filesystem::remove_all(root);
}

TEST_CASE("read reports missing paths", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::ReadTool read_tool(root);
  REQUIRE_THROWS(read_tool.execute(nlohmann::json{{"path", "missing.txt"}}));

  std::filesystem::remove_all(root);
}

TEST_CASE("read/write/edit reject symlink escapes outside workspace", "[ava_tools]") {
  const auto root = temp_root_for_test();
  const auto outside = temp_root_for_test() / "outside";
  std::filesystem::create_directories(root);
  std::filesystem::create_directories(outside);
  std::ofstream(outside / "secret.txt") << "secret";

  const auto link_path = root / "escape";
  std::error_code ec;
  std::filesystem::create_directory_symlink(outside, link_path, ec);
  if(ec) {
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside.parent_path());
    SKIP("filesystem does not permit symlink creation in this environment");
  }

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::ReadTool read_tool(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  REQUIRE_THROWS(read_tool.execute(nlohmann::json{{"path", "escape/secret.txt"}}));
  REQUIRE_THROWS(write_tool.execute(nlohmann::json{{"path", "escape/new.txt"}, {"content", "nope"}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "escape/secret.txt"}, {"old_text", "secret"}, {"new_text", "nope"}}));

  REQUIRE(read_text_file(outside / "secret.txt") == "secret");
  REQUIRE_FALSE(std::filesystem::exists(outside / "new.txt"));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside.parent_path());
}

TEST_CASE("write path guard rejects traversal and absolute workspace escapes", "[ava_tools]") {
  const auto root = temp_root_for_test();
  const auto outside_root = temp_root_for_test();
  std::filesystem::create_directories(root);
  std::filesystem::create_directories(outside_root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);

  const auto traversal_target_name = root.filename().string() + "_relative_escape.txt";
  const auto relative_outside = root.parent_path() / traversal_target_name;
  const auto absolute_outside = outside_root / "absolute-escape.txt";
  REQUIRE_THROWS(
      write_tool.execute(nlohmann::json{{"path", "../" + traversal_target_name}, {"content", "nope"}})
  );
  REQUIRE_THROWS(
      write_tool.execute(nlohmann::json{{"path", absolute_outside.string()}, {"content", "nope"}})
  );

  REQUIRE_FALSE(std::filesystem::exists(absolute_outside));
  REQUIRE_FALSE(std::filesystem::exists(relative_outside));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside_root);
}

TEST_CASE("write backup session skips new files and snapshots overwrites", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "tracked.txt"}, {"content", "v1"}});
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));

  require_write_ok(write_tool, nlohmann::json{{"path", "tracked.txt"}, {"content", "v2"}});
  REQUIRE(std::filesystem::exists(backup->backup_root()));

  std::vector<std::filesystem::path> backups;
  for(const auto& entry : std::filesystem::directory_iterator(backup->backup_root())) {
    if(entry.is_regular_file()) {
      backups.push_back(entry.path());
    }
  }

  REQUIRE_FALSE(backups.empty());

  bool found_previous_content = false;
  for(const auto& path : backups) {
    if(read_text_file(path) == "v1") {
      found_previous_content = true;
      break;
    }
  }

  REQUIRE(found_previous_content);
  REQUIRE(read_text_file(root / "tracked.txt") == "v2");
  std::filesystem::remove_all(root);
}

TEST_CASE("write backup session keeps snapshots for same filenames in different directories", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "src/main.rs"}, {"content", "src-v1"}});
  require_write_ok(write_tool, nlohmann::json{{"path", "tests/main.rs"}, {"content", "tests-v1"}});
  require_write_ok(write_tool, nlohmann::json{{"path", "src/main.rs"}, {"content", "src-v2"}});
  require_write_ok(write_tool, nlohmann::json{{"path", "tests/main.rs"}, {"content", "tests-v2"}});

  std::vector<std::string> backup_contents;
  for(const auto& entry : std::filesystem::directory_iterator(backup->backup_root())) {
    if(entry.is_regular_file()) {
      backup_contents.push_back(read_text_file(entry.path()));
    }
  }

  REQUIRE(backup_contents.size() == 2);
  REQUIRE(std::find(backup_contents.begin(), backup_contents.end(), "src-v1") != backup_contents.end());
  REQUIRE(std::find(backup_contents.begin(), backup_contents.end(), "tests-v1") != backup_contents.end());

  std::filesystem::remove_all(root);
}

TEST_CASE("edit backup session rejects symlinked ava directory", "[ava_tools]") {
  const auto root = temp_root_for_test();
  const auto outside = temp_root_for_test() / "backup-outside";
  std::filesystem::create_directories(root);
  std::filesystem::create_directories(outside);

  std::error_code ec;
  std::filesystem::create_directory_symlink(outside, root / ".ava", ec);
  if(ec) {
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside.parent_path());
    SKIP("filesystem does not permit symlink creation in this environment");
  }

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "tracked.txt"}, {"content", "before"}});
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "tracked.txt"}, {"old_text", "before"}, {"new_text", "after"}}));

  REQUIRE(read_text_file(root / "tracked.txt") == "before");
  REQUIRE_FALSE(std::filesystem::exists(outside / "file-history-m6"));
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside.parent_path());
}

TEST_CASE("write backup session rejects oversized existing file", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  const auto path = root / "large-existing.txt";
  std::ofstream out(path, std::ios::binary);
  out << std::string(8 * 1024 * 1024 + 1, 'x');
  out.close();

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);

  REQUIRE_THROWS(write_tool.execute(nlohmann::json{{"path", "large-existing.txt"}, {"content", "small"}}));
  REQUIRE(std::filesystem::file_size(path) == 8 * 1024 * 1024 + 1);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all updates all occurrences", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "many.txt"}, {"content", "x\nx\nx\n"}});

  const auto result = edit_tool.execute(nlohmann::json{{"path", "many.txt"},
                                                        {"old_text", "x"},
                                                        {"new_text", "y"},
                                                        {"replace_all", true}});

  REQUIRE(result.content.find("replace_all") != std::string::npos);
  REQUIRE(read_text_file(root / "many.txt") == "y\ny\ny\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all handles overlapping replacement safely", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "overlap.txt"}, {"content", "a a"}});

  const auto result = edit_tool.execute(nlohmann::json{{"path", "overlap.txt"},
                                                        {"old_text", "a"},
                                                        {"new_text", "aa"},
                                                        {"replace_all", true}});

  REQUIRE(result.content.find("replace_all") != std::string::npos);
  REQUIRE(read_text_file(root / "overlap.txt") == "aa aa");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all rejects explicit locators", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "many.txt"}, {"content", "x\nx\n"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "many.txt"},
                                                   {"old_text", "x"},
                                                   {"new_text", "y"},
                                                   {"replace_all", true},
                                                   {"occurrence", 2}}));
  REQUIRE(read_text_file(root / "many.txt") == "x\nx\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all supports deletion and no-match immutability", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "delete-all.txt"}, {"content", "red blue red"}});

  const auto result = edit_tool.execute(nlohmann::json{{"path", "delete-all.txt"},
                                                        {"old_text", "red"},
                                                        {"new_text", ""},
                                                        {"replace_all", true}});

  REQUIRE(result.content.find("replace_all") != std::string::npos);
  REQUIRE(read_text_file(root / "delete-all.txt") == " blue ");

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "delete-all.txt"},
                                                   {"old_text", "missing"},
                                                   {"new_text", "x"},
                                                   {"replace_all", true}}));
  REQUIRE(read_text_file(root / "delete-all.txt") == " blue ");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all rejects oversized input", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const std::string content(8 * 1024 * 1024 + 1, 'a');
  require_write_ok(write_tool, nlohmann::json{{"path", "huge-replace-all.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "huge-replace-all.txt"},
                                                   {"old_text", "a"},
                                                   {"new_text", "b"},
                                                   {"replace_all", true}}));
  REQUIRE(read_text_file(root / "huge-replace-all.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all bounds CRLF-normalized match text", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "crlf-bound.txt"}, {"content", "x\r\n"}});
  const auto old_text = std::string(4 * 1024 * 1024 + 1, '\n');

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "crlf-bound.txt"},
                                                   {"old_text", old_text},
                                                   {"new_text", "replacement"},
                                                   {"replace_all", true}}));
  REQUIRE(read_text_file(root / "crlf-bound.txt") == "x\r\n");
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all rejects oversized replacement text", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "huge-replacement.txt"}, {"content", "a"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "huge-replacement.txt"},
                                                   {"old_text", "a"},
                                                   {"new_text", std::string(4 * 1024 * 1024 + 1, 'b')},
                                                   {"replace_all", true}}));
  REQUIRE(read_text_file(root / "huge-replacement.txt") == "a");
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all allows large match text within replace-all bounds", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const std::string old_text(70 * 1024, 'x');
  require_write_ok(write_tool, nlohmann::json{{"path", "large-match.txt"}, {"content", old_text}});

  const auto result = edit_tool.execute(nlohmann::json{{"path", "large-match.txt"},
                                                        {"old_text", old_text},
                                                        {"new_text", "small"},
                                                        {"replace_all", true}});

  REQUIRE(result.content.find("replace_all") != std::string::npos);
  REQUIRE(read_text_file(root / "large-match.txt") == "small");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all rejects excessive replacement count without mutating", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const auto content = std::string(100001, 'x');
  require_write_ok(write_tool, nlohmann::json{{"path", "too-many.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "too-many.txt"},
                                                   {"old_text", "x"},
                                                   {"new_text", "y"},
                                                   {"replace_all", true}}));
  REQUIRE(read_text_file(root / "too-many.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit replace_all rejects oversized output without mutating", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const auto content = std::string(4097, 'x');
  require_write_ok(write_tool, nlohmann::json{{"path", "too-large-output.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "too-large-output.txt"},
                                                   {"old_text", "x"},
                                                   {"new_text", std::string(1024, 'y')},
                                                   {"replace_all", true}}));
  REQUIRE(read_text_file(root / "too-large-output.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit non replace rejects oversized replacement text before backup", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "single-large.txt"}, {"content", "needle"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "single-large.txt"},
                                                   {"old_text", "needle"},
                                                   {"new_text", std::string(8 * 1024 * 1024 + 1, 'z')}}));
  REQUIRE(read_text_file(root / "single-large.txt") == "needle");
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit non replace rejects oversized old text before backup", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "old-large.txt"}, {"content", "needle"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "old-large.txt"},
                                                   {"old_text", std::string(64 * 1024 + 1, 'n')},
                                                   {"new_text", "replacement"}}));
  REQUIRE(read_text_file(root / "old-large.txt") == "needle");
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit rejects multiple explicit locator families", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "multi-locator.txt"}, {"content", "alpha\nbeta\ngamma\n"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "multi-locator.txt"},
                                                   {"old_text", "beta"},
                                                   {"new_text", "BETA"},
                                                   {"line_number", 2},
                                                   {"occurrence", 1}}));
  REQUIRE(read_text_file(root / "multi-locator.txt") == "alpha\nbeta\ngamma\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit quote normalization matches curly quotes", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "quotes.txt"}, {"content", "const auto msg = \"hello\";\n"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "quotes.txt"},
                     {"old_text", "const auto msg = \xE2\x80\x9Chello\xE2\x80\x9D;"},
                     {"new_text", "const auto msg = \"goodbye\";"}}
  );

  REQUIRE(result.content.find("quote_normalized_exact_match") != std::string::npos);
  REQUIRE(read_text_file(root / "quotes.txt") == "const auto msg = \"goodbye\";\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit quote normalization matches curly quotes in content", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(
      write_tool,
      nlohmann::json{{"path", "quotes-content.txt"}, {"content", "const auto msg = \xE2\x80\x9Chello\xE2\x80\x9D;\n"}}
  );

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "quotes-content.txt"},
                     {"old_text", "const auto msg = \"hello\";"},
                     {"new_text", "const auto msg = \"goodbye\";"}}
  );

  REQUIRE(result.content.find("quote_normalized_exact_match") != std::string::npos);
  REQUIRE(read_text_file(root / "quotes-content.txt") == "const auto msg = \"goodbye\";\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit occurrence parameter replaces nth occurrence", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "occurrence.txt"}, {"content", "x\nx\nx\n"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "occurrence.txt"},
                     {"old_text", "x"},
                     {"new_text", "y"},
                     {"occurrence", 2}}
  );

  REQUIRE(result.content.find("occurrence_match") != std::string::npos);
  REQUIRE(read_text_file(root / "occurrence.txt") == "x\ny\nx\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit occurrence parameter supports first occurrence explicitly", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "occurrence-first.txt"}, {"content", "x\nx\n"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "occurrence-first.txt"}, {"old_text", "x"}, {"new_text", "y"}, {"occurrence", 1}}
  );

  REQUIRE(result.content.find("occurrence_match") != std::string::npos);
  REQUIRE(read_text_file(root / "occurrence-first.txt") == "y\nx\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit explicit locators fail closed without broad fallback", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "locator.txt"}, {"content", "alpha\nbeta\ngamma\n"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "locator.txt"},
                                                   {"old_text", "beta"},
                                                   {"new_text", "BETA"},
                                                   {"line_number", 99}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "locator.txt"},
                                                   {"old_text", "beta"},
                                                   {"new_text", "BETA"},
                                                   {"occurrence", 2}}));

  REQUIRE(read_text_file(root / "locator.txt") == "alpha\nbeta\ngamma\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit locator parameters reject invalid JSON types with deterministic errors", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "locator-types.txt"}, {"content", "alpha\nbeta\n"}});

  try {
    [[maybe_unused]] const auto result = edit_tool.execute(nlohmann::json{{"path", "locator-types.txt"},
                                                                           {"old_text", "alpha"},
                                                                           {"new_text", "ALPHA"},
                                                                           {"occurrence", "first"}});
    FAIL("expected invalid occurrence type to fail");
  } catch(const std::runtime_error& error) {
    REQUIRE(std::string(error.what()).find("occurrence must be a positive integer") != std::string::npos);
  }

  try {
    [[maybe_unused]] const auto result = edit_tool.execute(nlohmann::json{{"path", "locator-types.txt"},
                                                                           {"old_text", "alpha"},
                                                                           {"new_text", "ALPHA"},
                                                                           {"line_number", -1}});
    FAIL("expected invalid line_number value to fail");
  } catch(const std::runtime_error& error) {
    REQUIRE(std::string(error.what()).find("line_number must be >= 1") != std::string::npos);
  }

  REQUIRE(read_text_file(root / "locator-types.txt") == "alpha\nbeta\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit line strategies preserve CRLF line endings", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "crlf.txt"}, {"content", "alpha\r\nbeta\r\ngamma\r\n"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "crlf.txt"}, {"old_text", "beta"}, {"new_text", "BETA"}, {"line_number", 2}}
  );

  REQUIRE(result.content.find("line_number") != std::string::npos);
  REQUIRE(read_text_file(root / "crlf.txt") == "alpha\r\nBETA\r\ngamma\r\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit line_number supports inline and whole-line replacements", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  SECTION("inline replacement") {
    require_write_ok(write_tool, nlohmann::json{{"path", "line-inline.txt"}, {"content", "alpha\nbeta value\ngamma\n"}});

    const auto result = require_edit_ok(
        edit_tool,
        nlohmann::json{{"path", "line-inline.txt"},
                       {"old_text", "value"},
                       {"new_text", "VALUE"},
                       {"line_number", 2}}
    );

    REQUIRE(result.content.find("line_number") != std::string::npos);
    REQUIRE(read_text_file(root / "line-inline.txt") == "alpha\nbeta VALUE\ngamma\n");
  }

  SECTION("whole-line replacement") {
    require_write_ok(write_tool, nlohmann::json{{"path", "line-whole.txt"}, {"content", "alpha\nbeta\ngamma\n"}});

    const auto result = require_edit_ok(
        edit_tool,
        nlohmann::json{{"path", "line-whole.txt"},
                       {"old_text", "beta"},
                       {"new_text", "BETA WHOLE"},
                       {"line_number", 2}}
    );

    REQUIRE(result.content.find("line_number") != std::string::npos);
    REQUIRE(read_text_file(root / "line-whole.txt") == "alpha\nBETA WHOLE\ngamma\n");
  }

  std::filesystem::remove_all(root);
}

TEST_CASE("edit explicit anchors replace inside bounded region", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "anchors.txt"}, {"content", "A<before>line old line<after>Z"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "anchors.txt"},
                     {"old_text", "old"},
                     {"new_text", "NEW"},
                     {"before_anchor", "<before>"},
                     {"after_anchor", "<after>"}}
  );

  REQUIRE(result.content.find("block_anchor") != std::string::npos);
  REQUIRE(read_text_file(root / "anchors.txt") == "A<before>line NEW line<after>Z");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit explicit anchors fail closed on empty or incomplete regions", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "anchors-fail.txt"}, {"content", "A<before>line old line<after>Z"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "anchors-fail.txt"},
                                                   {"old_text", "old"},
                                                   {"new_text", "NEW"},
                                                   {"before_anchor", ""},
                                                   {"after_anchor", "<after>"}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "anchors-fail.txt"},
                                                   {"old_text", "old"},
                                                   {"new_text", "NEW"},
                                                   {"before_anchor", "<before>"},
                                                   {"after_anchor", "<missing>"}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "anchors-fail.txt"},
                                                   {"old_text", "absent"},
                                                   {"new_text", "NEW"},
                                                   {"before_anchor", "<before>"},
                                                   {"after_anchor", "<after>"}}));
  REQUIRE(read_text_file(root / "anchors-fail.txt") == "A<before>line old line<after>Z");
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit line-trimmed strategy handles indentation mismatch", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(
      write_tool,
      nlohmann::json{{"path", "line-trimmed.txt"},
                     {"content", "fn main() {\n    let x = 1;\n    let y = 2;\n}\n"}}
  );

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "line-trimmed.txt"},
                     {"old_text", "let x = 1;\nlet y = 2;"},
                     {"new_text", "    let x = 10;\n    let y = 20;"}}
  );

  REQUIRE(result.content.find("line_trimmed") != std::string::npos);
  REQUIRE(read_text_file(root / "line-trimmed.txt") == "fn main() {\n    let x = 10;\n    let y = 20;\n}\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit line-trimmed strategy rejects ambiguous matches", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const auto content = std::string("  let x = 1;\n  let y = 2;\n\tlet x = 1;\n\tlet y = 2;\n");
  require_write_ok(write_tool, nlohmann::json{{"path", "line-trimmed-ambiguous.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "line-trimmed-ambiguous.txt"},
                                                   {"old_text", "let x = 1;\nlet y = 2;"},
                                                   {"new_text", "changed"}}));
  REQUIRE(read_text_file(root / "line-trimmed-ambiguous.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit auto block anchor matches first and last non-empty lines", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(
      write_tool,
      nlohmann::json{{"path", "auto-anchor.txt"},
                     {"content", "fn main() {\n    if ready {\n        run();\n    }\n}\n"}}
  );

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "auto-anchor.txt"},
                     {"old_text", "if ready {\n    something_else();\n}"},
                     {"new_text", "    if ready {\n        run_fast();\n    }"}}
  );

  REQUIRE(result.content.find("auto_block_anchor") != std::string::npos);
  REQUIRE(read_text_file(root / "auto-anchor.txt") == "fn main() {\n    if ready {\n        run_fast();\n    }\n}\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit auto block anchor rejects ambiguous or degenerate anchors", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const auto content = std::string("if ready {\n  one();\n}\nif ready {\n  two();\n}\n");
  require_write_ok(write_tool, nlohmann::json{{"path", "auto-anchor-ambiguous.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "auto-anchor-ambiguous.txt"},
                                                   {"old_text", "if ready {\n  missing();\n}"},
                                                   {"new_text", "changed"}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "auto-anchor-ambiguous.txt"},
                                                   {"old_text", "if ready {\n  missing();\nif ready {"},
                                                   {"new_text", "changed"}}));
  REQUIRE(read_text_file(root / "auto-anchor-ambiguous.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit ellipsis strategy matches comment placeholders", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(
      write_tool,
      nlohmann::json{{"path", "ellipsis.txt"},
                     {"content", "if ready {\n    let x = 1;\n    do_main();\n}\nif ready {\n    do_other();\n}\n"}}
  );

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "ellipsis.txt"},
                     {"old_text", "if ready {\n    // ...\n    do_main();\n}"},
                     {"new_text", "if ready {\n    do_main_fast();\n}"}}
  );

  REQUIRE(result.content.find("ellipsis") != std::string::npos);
  REQUIRE(read_text_file(root / "ellipsis.txt") ==
          "if ready {\n    do_main_fast();\n}\nif ready {\n    do_other();\n}\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit ellipsis strategy fails closed when fragments are insufficient or missing", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const auto content = std::string("start\nmiddle\nend\n");
  require_write_ok(write_tool, nlohmann::json{{"path", "ellipsis-fail.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "ellipsis-fail.txt"},
                                                   {"old_text", "start\n...\n"},
                                                   {"new_text", "changed"}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "ellipsis-fail.txt"},
                                                   {"old_text", "start\n...\nmissing"},
                                                   {"new_text", "changed"}}));
  REQUIRE(read_text_file(root / "ellipsis-fail.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit flexible whitespace strategy matches spacing variations", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "flex.txt"}, {"content", "Alpha    beta\tgamma\n"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "flex.txt"}, {"old_text", "alpha beta gamma"}, {"new_text", "delta"}}
  );

  REQUIRE(result.content.find("flexible_whitespace") != std::string::npos);
  REQUIRE(read_text_file(root / "flex.txt") == "delta\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit flexible whitespace strategy rejects ambiguous matches", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  const auto content = std::string("Alpha    beta gamma\nalpha beta   gamma\n");
  require_write_ok(write_tool, nlohmann::json{{"path", "flex-ambiguous.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "flex-ambiguous.txt"},
                                                   {"old_text", "alpha beta gamma"},
                                                   {"new_text", "delta"}}));
  REQUIRE(read_text_file(root / "flex-ambiguous.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit non replace supports deletion", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "delete-single.txt"}, {"content", "alpha beta gamma"}});

  const auto result = require_edit_ok(
      edit_tool,
      nlohmann::json{{"path", "delete-single.txt"}, {"old_text", " beta"}, {"new_text", ""}}
  );

  REQUIRE(result.content.find("exact_match") != std::string::npos);
  REQUIRE(read_text_file(root / "delete-single.txt") == "alpha gamma");
  std::filesystem::remove_all(root);
}

TEST_CASE("edit rejects empty old_text", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "empty.txt"}, {"content", "abc"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "empty.txt"},
                                                  {"old_text", ""},
                                                  {"new_text", "x"},
                                                  {"replace_all", true}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "empty.txt"},
                                                  {"old_text", ""},
                                                  {"new_text", "x"}}));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit no match returns error without mutating file", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "nomatch.txt"}, {"content", "alpha\nbeta\n"}});
  try {
    (void) edit_tool.execute(nlohmann::json{{"path", "nomatch.txt"}, {"old_text", "gamma"}, {"new_text", "delta"}});
    FAIL("expected edit no-match to throw");
  } catch(const std::exception& error) {
    REQUIRE(std::string(error.what()).find("No matching text found for edit") != std::string::npos);
  }
  REQUIRE(read_text_file(root / "nomatch.txt") == "alpha\nbeta\n");

  std::filesystem::remove_all(root);
}

TEST_CASE("edit cascade no match keeps file immutable", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  require_write_ok(write_tool, nlohmann::json{{"path", "cascade-nomatch.txt"}, {"content", "alpha\nbeta\ngamma\n"}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "cascade-nomatch.txt"},
                                                   {"old_text", "if missing {\n...\n}"},
                                                   {"new_text", "replacement"},
                                                   {"before_anchor", "<start>"},
                                                   {"after_anchor", "<end>"}}));

  REQUIRE(read_text_file(root / "cascade-nomatch.txt") == "alpha\nbeta\ngamma\n");
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("edit skips broad cascade beyond size limits", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::EditTool edit_tool(root, backup);

  std::string content(2 * 1024 * 1024 + 1, 'x');
  content += "\n  alpha line\n  beta line\n";
  require_write_ok(write_tool, nlohmann::json{{"path", "large-cascade.txt"}, {"content", content}});

  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", "large-cascade.txt"},
                                                   {"old_text", "alpha line\nbeta line"},
                                                   {"new_text", "changed"}}));
  REQUIRE(read_text_file(root / "large-cascade.txt") == content);
  REQUIRE_FALSE(std::filesystem::exists(backup->backup_root()));
  std::filesystem::remove_all(root);
}

TEST_CASE("glob and grep find files and content", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root / "src");

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::GlobTool glob_tool(root);
  ava::tools::GrepTool grep_tool(root);

  require_write_ok(write_tool, nlohmann::json{{"path", "src/a.rs"}, {"content", "let status = 1;\n"}});
  require_write_ok(write_tool, nlohmann::json{{"path", "src/b.txt"}, {"content", "status: ok\n"}});

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

TEST_CASE("glob and grep follow safe symlinks and skip escapes", "[ava_tools]") {
  const auto root = temp_root_for_test();
  const auto outside = temp_root_for_test() / "outside";
  std::filesystem::create_directories(root / "src");
  std::filesystem::create_directories(outside);

  std::ofstream(root / "src" / "real.txt") << "safe needle\n";
  std::ofstream(outside / "secret.txt") << "secret needle\n";

  std::error_code ec;
  std::filesystem::create_symlink(root / "src" / "real.txt", root / "src" / "safe-link.txt", ec);
  if(ec) {
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside.parent_path());
    SKIP("filesystem does not permit symlink creation in this environment");
  }
  std::filesystem::create_symlink(outside / "secret.txt", root / "src" / "escape-link.txt", ec);
  if(ec) {
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside.parent_path());
    SKIP("filesystem does not permit external symlink creation in this environment");
  }
  std::filesystem::create_symlink(root / "src" / "missing.txt", root / "src" / "broken-link.txt", ec);
  if(ec) {
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside.parent_path());
    SKIP("filesystem does not permit broken symlink creation in this environment");
  }

  ava::tools::GlobTool glob_tool(root);
  ava::tools::GrepTool grep_tool(root);

  const auto glob_result = glob_tool.execute(nlohmann::json{{"pattern", "**/*link.txt"}, {"path", "src"}}).content;
  REQUIRE(glob_result.find("escape-link.txt") == std::string::npos);

  const auto grep_result = grep_tool.execute(nlohmann::json{{"pattern", "needle"}, {"path", "src"}}).content;
  REQUIRE(grep_result.find("safe-link.txt:1:safe needle") != std::string::npos);
  REQUIRE(grep_result.find("escape-link.txt") == std::string::npos);
  REQUIRE(grep_result.find("broken-link.txt") == std::string::npos);
  REQUIRE(grep_result.find("secret needle") == std::string::npos);

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside.parent_path());
}

TEST_CASE("bash executes command with captured output", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::BashTool bash_tool(root);
  const auto result = bash_tool.execute(nlohmann::json{{"command", "printf 'hello'"}});

  REQUIRE(result.content.find("hello") != std::string::npos);
  REQUIRE(result.content.find("exit_code: 0") != std::string::npos);
  REQUIRE(result.content.find("Output truncated by bash") == std::string::npos);
  REQUIRE_FALSE(result.is_error);
  std::filesystem::remove_all(root);
}

TEST_CASE("bash applies output fallback truncation for oversized output", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::BashTool bash_tool(root);
  const auto result = bash_tool.execute(nlohmann::json{{"command", "yes x | head -c 70000"}});

  REQUIRE_FALSE(result.is_error);
  REQUIRE(result.content.find("Output truncated by bash") != std::string::npos);
  REQUIRE(result.content.size() < 50000);
  std::filesystem::remove_all(root);
}

TEST_CASE("bash reports non-zero exit codes as tool errors", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::tools::BashTool bash_tool(root);
  const auto result = bash_tool.execute(nlohmann::json{{"command", "sh -c 'exit 7'"}});

  REQUIRE(result.is_error);
  REQUIRE(result.content.find("exit_code: 7") != std::string::npos);
  std::filesystem::remove_all(root);
}

TEST_CASE("git and git_read execute read-only git commands", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  CwdGuard cwd_guard;
  std::filesystem::current_path(root);
  REQUIRE(std::system("git init >/dev/null 2>&1") == 0);

  ava::tools::GitReadTool git_tool(root);
  const auto status_result = git_tool.execute(nlohmann::json{{"command", "status --short"}});
  REQUIRE(status_result.content.find("exit_code: 0") != std::string::npos);

  REQUIRE_NOTHROW(git_tool.execute(nlohmann::json{{"command", "branch -v"}}));
  REQUIRE_NOTHROW(git_tool.execute(nlohmann::json{{"command", "branch -a"}}));
  REQUIRE_NOTHROW(git_tool.execute(nlohmann::json{{"command", "branch -r"}}));
  REQUIRE_NOTHROW(git_tool.execute(nlohmann::json{{"command", "branch --show-current"}}));
  REQUIRE_NOTHROW(git_tool.execute(nlohmann::json{{"command", "tag -n"}}));

  ava::tools::GitReadAliasTool git_read_tool(root);
  const auto log_result = git_read_tool.execute(nlohmann::json{{"command", "status --short"}});
  REQUIRE(log_result.content.find("exit_code: 0") != std::string::npos);

  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "push origin main"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "branch -D main"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "branch attacker-controlled-ref HEAD"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "branch --force attacker-controlled-ref HEAD"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "branch -v -D temp"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "tag -f v1.0"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "remote -v set-url origin https://attacker.invalid/repo.git"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "status --short; status --short"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "diff --no-index /etc/hosts /dev/null"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "diff '--no-index' '/etc/hosts' '/dev/null'"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "diff --no-inde[x] .ava/file-history-m[6] baseline"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "diff --output=/tmp/out HEAD"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "diff --ext-diff HEAD"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "show --textconv HEAD:file.txt"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "blame --contents=/etc/hosts -- README.md"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "blame --contents=.ava/*/*/*.bak -- README.md"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "blame --ignore-revs-file=.ava/file-history-m6/revs -- README.md"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "status --ignored -uall"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "status .ava/file-history-m6/snapshot.bak"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "show /etc/hosts"}}));
  REQUIRE_THROWS(git_tool.execute(nlohmann::json{{"command", "show ../outside"}}));

  std::filesystem::remove_all(root);
}

TEST_CASE("read glob and grep do not expose file-history backups", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::ReadTool read_tool(root);
  ava::tools::EditTool edit_tool(root, backup);
  ava::tools::GlobTool glob_tool(root);
  ava::tools::GrepTool grep_tool(root);

  require_write_ok(write_tool, nlohmann::json{{"path", "secret.txt"}, {"content", "old-secret-token"}});
  require_write_ok(write_tool, nlohmann::json{{"path", "secret.txt"}, {"content", "redacted"}});
  REQUIRE(std::filesystem::exists(backup->backup_root()));

  std::filesystem::path backup_file;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(backup->backup_root())) {
    if(entry.is_regular_file()) {
      backup_file = entry.path();
      break;
    }
  }
  REQUIRE_FALSE(backup_file.empty());
  const auto relative_backup = std::filesystem::relative(backup_file, root).generic_string();

  const auto ava_listing = read_tool.execute(nlohmann::json{{"path", ".ava"}}).content;
  REQUIRE(ava_listing.find("file-history-m6") == std::string::npos);
  REQUIRE_THROWS(read_tool.execute(nlohmann::json{{"path", relative_backup}}));
  REQUIRE_THROWS(write_tool.execute(nlohmann::json{{"path", relative_backup}, {"content", "tamper"}}));
  REQUIRE_THROWS(edit_tool.execute(nlohmann::json{{"path", relative_backup}, {"old_text", "old-secret-token"}, {"new_text", "tamper"}}));
  const auto glob_result = glob_tool.execute(nlohmann::json{{"pattern", "**/*.bak"}, {"path", "."}}).content;
  REQUIRE(glob_result.find("file-history-m6") == std::string::npos);
  const auto grep_result = grep_tool.execute(nlohmann::json{{"pattern", "old-secret-token"}, {"path", "."}}).content;
  REQUIRE(grep_result.find("old-secret-token") == std::string::npos);

  std::filesystem::remove_all(root);
}

TEST_CASE("glob matches root-level files for doublestar patterns", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::GlobTool glob_tool(root);

  require_write_ok(write_tool, nlohmann::json{{"path", "root.rs"}, {"content", "fn main() {}\n"}});
  const auto result = glob_tool.execute(nlohmann::json{{"pattern", "**/*.rs"}, {"path", "."}}).content;
  REQUIRE(result.find("root.rs") != std::string::npos);

  std::filesystem::remove_all(root);
}

TEST_CASE("glob returns files and skips directories", "[ava_tools]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root / "dir.rs");

  auto backup = std::make_shared<ava::tools::FileBackupSession>(root);
  ava::tools::WriteTool write_tool(root, backup);
  ava::tools::GlobTool glob_tool(root);

  require_write_ok(write_tool, nlohmann::json{{"path", "file.rs"}, {"content", "fn main() {}\n"}});
  const auto result = glob_tool.execute(nlohmann::json{{"pattern", "*.rs"}, {"path", "."}}).content;
  REQUIRE(result.find("file.rs") != std::string::npos);
  REQUIRE(result.find("dir.rs") == std::string::npos);

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
