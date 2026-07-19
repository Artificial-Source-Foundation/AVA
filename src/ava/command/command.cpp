#include "sys.h"
#include "ava/command/command.h"
#include "ava/core/error.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::command {
namespace {

constexpr std::size_t kHardMaxRequestBytes = 16 * 1024;
constexpr std::size_t kHardMaxArgumentBytes = 4 * 1024;
constexpr std::size_t kHardMaxArgvEntries = 128;
constexpr std::size_t kHardMaxPathEntries = 64;
constexpr std::size_t kHardMaxPathBytes = 16 * 1024;
constexpr std::size_t kHardMaxShebangBytes = 512;
constexpr std::size_t kHardMaxShebangDepth = 4;

ava::core::Error command_error(ava::core::ErrorCategory category, std::string message, std::string_view field = {}, std::string_view value = {})
{
  ava::core::Error error(category, std::move(message));
  if (!field.empty())
    error.with_context(std::string(field), std::string(value));
  return error;
}

bool has_forbidden_byte(std::string_view value)
{
  for (unsigned char const byte : value)
  {
    if (byte == 0 || byte < 0x20 || byte == 0x7f)
      return true;
  }
  return false;
}

bool has_forbidden_path_byte(std::filesystem::path const& path)
{
  return has_forbidden_byte(path.string());
}

ava::core::VoidResult validate_limits(CommandLimits const& limits)
{
  auto const invalid = [](std::size_t value, std::size_t hard_max) { return value == 0 || value > hard_max; };
  if (invalid(limits.max_request_bytes, kHardMaxRequestBytes) || invalid(limits.max_argument_bytes, kHardMaxArgumentBytes) ||
      invalid(limits.max_argv_entries, kHardMaxArgvEntries) || invalid(limits.max_path_entries, kHardMaxPathEntries) ||
      invalid(limits.max_path_bytes, kHardMaxPathBytes) || invalid(limits.max_shebang_bytes, kHardMaxShebangBytes) ||
      invalid(limits.max_shebang_depth, kHardMaxShebangDepth))
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command limits must be non-zero and within fixed safety bounds"));
  }
  return {};
}

bool is_shell_metacharacter(char ch)
{
  switch (ch)
  {
    case ';':
    case '&':
    case '|':
    case '<':
    case '>':
    case '`':
    case '$':
    case '(':
    case ')':
    case '*':
    case '?':
    case '[':
    case ']':
    case '{':
    case '}':
    case '~':
    case '#':
    case '!':
      return true;
    default:
      return false;
  }
}

struct CompatibilityParse
{
  std::vector<std::string> argv;
  bool has_unquoted_shell_metacharacter = false;
};

ava::core::Result<CompatibilityParse> parse_compatibility_command(std::string_view command, CommandLimits const& limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (command.empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request must not be empty"));
  if (command.size() > limits.max_request_bytes)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request exceeds the bounded input size"));
  if (has_forbidden_byte(command))
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request contains a NUL or forbidden control byte"));

  CompatibilityParse parsed;
  std::string current;
  char quote = '\0';
  bool escaping = false;
  bool token_started = false;

  auto append_current = [&]() -> ava::core::VoidResult {
    if (!token_started)
      return {};
    if (current.size() > limits.max_argument_bytes)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command argument exceeds the bounded field size"));
    if (parsed.argv.size() == limits.max_argv_entries)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request has too many argv entries"));
    parsed.argv.push_back(std::move(current));
    current.clear();
    token_started = false;
    return {};
  };

  for (char const ch : command)
  {
    if (escaping)
    {
      current.push_back(ch);
      token_started = true;
      escaping = false;
      continue;
    }
    if (ch == '\\')
    {
      token_started = true;
      escaping = true;
      continue;
    }
    if (quote != '\0')
    {
      if (ch == quote)
      {
        quote = '\0';
      }
      else
      {
        current.push_back(ch);
      }
      token_started = true;
      continue;
    }
    if (ch == '\'' || ch == '"')
    {
      quote = ch;
      token_started = true;
      continue;
    }
    if (is_shell_metacharacter(ch))
    {
      parsed.has_unquoted_shell_metacharacter = true;
      token_started = true;
      current.push_back(ch);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0)
    {
      if (auto appended = append_current(); !appended)
        return std::unexpected(std::move(appended.error()));
      continue;
    }
    token_started = true;
    current.push_back(ch);
  }

  if (escaping || quote != '\0')
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request contains an unterminated quote or escape"));
  }
  if (auto appended = append_current(); !appended)
    return std::unexpected(std::move(appended.error()));
  if (parsed.argv.empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request must encode a non-empty executable argument"));
  if (parsed.argv.front().empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command request has an ambiguous empty executable argument"));
  return parsed;
}

ava::core::VoidResult validate_structured_argv(std::vector<std::string> const& argv, CommandLimits const& limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return valid;
  if (argv.empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "structured command argv must not be empty"));
  if (argv.size() > limits.max_argv_entries)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "structured command has too many argv entries"));
  if (argv.front().empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "structured command has an ambiguous empty executable argument"));

  std::size_t total_size = 0;
  for (auto const& argument : argv)
  {
    if (argument.size() > limits.max_argument_bytes)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "structured command argument exceeds the bounded field size"));
    if (has_forbidden_byte(argument))
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "structured command argument contains a NUL or forbidden control byte"));
    if (argument.size() > limits.max_request_bytes - total_size)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "structured command exceeds the bounded request size"));
    total_size += argument.size();
  }
  return {};
}

ava::core::VoidResult validate_raw_shell(std::string_view shell_text, CommandLimits const& limits)
{
  auto parsed = parse_compatibility_command(shell_text, limits);
  if (!parsed)
    return std::unexpected(std::move(parsed.error()));
  return {};
}

std::string lowercase(std::string_view value)
{
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

bool is_within(std::filesystem::path const& child, std::filesystem::path const& parent)
{
  std::error_code error;
  auto relative = std::filesystem::relative(child, parent, error);
  if (error)
    return false;
  if (relative.empty() || relative == ".")
    return true;
  auto const begin = relative.begin();
  return begin != relative.end() && *begin != "..";
}

ava::core::Result<std::filesystem::path> canonical_directory(std::filesystem::path const& requested, bool reject_direct_symlink, std::string_view label)
{
  if (requested.empty() || has_forbidden_path_byte(requested))
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, std::string(label) + " must not be empty or contain control bytes", "path",
                                         requested.string()));
  }

  std::error_code status_error;
  auto const direct_status = std::filesystem::symlink_status(requested, status_error);
  if (status_error || !std::filesystem::is_directory(direct_status))
  {
    auto error = command_error(ava::core::ErrorCategory::InvalidArgument, std::string(label) + " must be an existing directory", "path", requested.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (reject_direct_symlink && std::filesystem::is_symlink(direct_status))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::PermissionDenied, std::string(label) + " must not be a symlink", "path", requested.string()));
  }

  std::error_code canonical_error;
  auto canonical = std::filesystem::canonical(requested, canonical_error);
  if (canonical_error)
  {
    auto error = command_error(ava::core::ErrorCategory::Io, "failed to canonicalize " + std::string(label), "path", requested.string());
    error.with_context("cause", canonical_error.message());
    return std::unexpected(std::move(error));
  }
  return canonical;
}

ava::core::Result<std::filesystem::path> canonical_workspace(std::filesystem::path const& workspace)
{
  return canonical_directory(workspace, true, "command workspace");
}

ava::core::Result<std::filesystem::path> canonical_cwd(CommandIntent const& intent, std::filesystem::path const& workspace)
{
  auto requested = intent.requested_cwd();
  if (!requested)
    return workspace;
  auto candidate = requested->is_absolute() ? *requested : workspace / *requested;
  auto cwd = canonical_directory(candidate, true, "command cwd");
  if (!cwd)
    return std::unexpected(std::move(cwd.error()));
  if (!is_within(*cwd, workspace))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::PermissionDenied, "command cwd must remain inside the canonical workspace", "cwd", cwd->string()));
  }
  return cwd;
}

bool path_mode_is_safe(struct stat const& status)
{
  return (status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

bool path_owner_is_safe(struct stat const& status, bool require_current_user)
{
  if (require_current_user)
    return status.st_uid == geteuid();
  return status.st_uid == geteuid() || status.st_uid == 0;
}

ava::core::Result<std::filesystem::path> safe_path_directory(std::filesystem::path const& requested, bool allow_direct_symlink, bool require_current_user,
                                                             std::string_view provenance)
{
  if (requested.empty() || !requested.is_absolute())
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "PATH entry must be an absolute non-empty directory", "path", requested.string()));
  }
  if (has_forbidden_path_byte(requested) || requested.string().find(':') != std::string::npos)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "PATH entry contains unsafe bytes", "path", requested.string()));
  }

  std::error_code direct_error;
  auto const direct_status = std::filesystem::symlink_status(requested, direct_error);
  auto const direct_is_symlink = !direct_error && std::filesystem::is_symlink(direct_status);
  if (direct_error || !std::filesystem::exists(direct_status) || (!direct_is_symlink && !std::filesystem::is_directory(direct_status)))
  {
    auto error = command_error(ava::core::ErrorCategory::InvalidArgument, "PATH entry must name an existing directory", "path", requested.string());
    if (direct_error)
      error.with_context("cause", direct_error.message());
    return std::unexpected(std::move(error));
  }
  if (!allow_direct_symlink && direct_is_symlink)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, "PATH candidate must not be a symlink", "path", requested.string()));
  }

  std::error_code canonical_error;
  auto canonical = std::filesystem::canonical(requested, canonical_error);
  if (canonical_error)
  {
    auto error = command_error(ava::core::ErrorCategory::Io, "failed to canonicalize PATH entry", "path", requested.string());
    error.with_context("cause", canonical_error.message());
    return std::unexpected(std::move(error));
  }
  struct stat status{};
  if (::stat(canonical.c_str(), &status) != 0 || !S_ISDIR(status.st_mode))
  {
    auto error = command_error(ava::core::ErrorCategory::Io, "failed to inspect canonical PATH directory", "path", canonical.string());
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  if (!path_mode_is_safe(status) || !path_owner_is_safe(status, require_current_user))
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, "PATH directory has unsafe ownership or group/world-writable permissions",
                                         "path", canonical.string()));
  }
  if (canonical.string().find(':') != std::string::npos)
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "canonical PATH directory cannot be represented safely", "path", canonical.string()));
  }
  static_cast<void>(provenance);
  return canonical;
}

void append_unique_path(std::vector<CommandPathEntry>& entries, std::filesystem::path const& directory, PathProvenance provenance)
{
  auto const found = std::ranges::find_if(entries, [&directory](CommandPathEntry const& entry) { return entry.directory == directory; });
  if (found == entries.end())
    entries.push_back(CommandPathEntry{.directory = directory, .provenance = provenance});
}

std::vector<std::string_view> split_path(std::string_view path)
{
  std::vector<std::string_view> entries;
  std::size_t start = 0;
  while (true)
  {
    auto const separator = path.find(':', start);
    entries.push_back(path.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start));
    if (separator == std::string_view::npos)
      break;
    start = separator + 1;
  }
  return entries;
}

ava::core::Result<std::vector<CommandPathEntry>> discover_path(CommandBuildOptions const& options, std::filesystem::path const& workspace)
{
  if (auto valid = validate_limits(options.limits); !valid)
    return std::unexpected(std::move(valid.error()));

  std::optional<std::string> startup_path = options.startup_path;
  if (!startup_path)
  {
    if (char const* current = std::getenv("PATH"))
      startup_path = current;
  }
  if (startup_path && startup_path->size() > options.limits.max_path_bytes)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH exceeds the bounded input size"));
  if (startup_path && has_forbidden_byte(*startup_path))
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH contains a forbidden control byte"));

  std::vector<CommandPathEntry> entries;
  if (startup_path)
  {
    auto const startup_entries = split_path(*startup_path);
    if (startup_entries.size() > options.limits.max_path_entries)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH has too many entries"));
    for (auto const entry : startup_entries)
    {
      if (entry.empty())
        return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "startup PATH contains an empty entry"));
      auto safe = safe_path_directory(std::filesystem::path(entry), true, false, "startup PATH");
      if (!safe)
        return std::unexpected(std::move(safe.error()));
      append_unique_path(entries, *safe, PathProvenance::StartupPath);
    }
  }

  auto const add_optional_candidate = [&](std::filesystem::path const& candidate, PathProvenance provenance) {
    std::error_code exists_error;
    if (!std::filesystem::exists(candidate, exists_error) || exists_error)
      return;
    auto safe = safe_path_directory(candidate, false, true, "candidate");
    if (safe)
      append_unique_path(entries, *safe, provenance);
  };
  add_optional_candidate(options.environment.home / ".local" / "bin", PathProvenance::UserLocal);
  add_optional_candidate(options.environment.home / ".cargo" / "bin", PathProvenance::UserCargo);
  add_optional_candidate(workspace / ".venv" / "bin", PathProvenance::WorkspaceVenv);
  add_optional_candidate(workspace / "node_modules" / ".bin", PathProvenance::WorkspaceNodeModules);

  if (entries.size() > options.limits.max_path_entries)
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "derived PATH has too many safe entries"));
  std::size_t total_bytes = 0;
  for (auto const& entry : entries)
  {
    auto const bytes = entry.directory.string().size();
    if (bytes > options.limits.max_path_bytes - total_bytes)
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "derived PATH exceeds the bounded size"));
    total_bytes += bytes;
  }
  return entries;
}

ava::core::Result<ExecutableMetadata> executable_metadata(std::filesystem::path const& requested)
{
  std::error_code canonical_error;
  auto canonical = std::filesystem::canonical(requested, canonical_error);
  if (canonical_error)
  {
    auto error = command_error(ava::core::ErrorCategory::NotFound, "failed to canonicalize executable", "path", requested.string());
    error.with_context("cause", canonical_error.message());
    return std::unexpected(std::move(error));
  }
  struct stat status{};
  if (::stat(canonical.c_str(), &status) != 0)
  {
    auto error = command_error(ava::core::ErrorCategory::Io, "failed to inspect executable", "path", canonical.string());
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(status.st_mode))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "resolved command is not a regular executable file", "path", canonical.string()));
  }
  if ((status.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::PermissionDenied, "resolved command is not executable", "path", canonical.string()));
  }

  std::error_code time_error;
  auto const modified = std::filesystem::last_write_time(canonical, time_error);
  if (time_error)
  {
    auto error = command_error(ava::core::ErrorCategory::Io, "failed to read executable modification time", "path", canonical.string());
    error.with_context("cause", time_error.message());
    return std::unexpected(std::move(error));
  }
  return ExecutableMetadata{.canonical_path = std::move(canonical),
                            .device = static_cast<std::uintmax_t>(status.st_dev),
                            .inode = static_cast<std::uintmax_t>(status.st_ino),
                            .mode = static_cast<std::uintmax_t>(status.st_mode),
                            .size = static_cast<std::uintmax_t>(status.st_size),
                            .modified_ticks = static_cast<std::int64_t>(modified.time_since_epoch().count())};
}

std::filesystem::path normalize_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return normalized;
  return std::filesystem::absolute(path, error).lexically_normal();
}

ExecutableOrigin executable_origin(ExecutableMetadata const& executable, std::filesystem::path const& workspace, std::filesystem::path const& synthetic_home)
{
  if (is_within(executable.canonical_path, workspace))
    return ExecutableOrigin::Workspace;
  if (!synthetic_home.empty() && is_within(executable.canonical_path, normalize_path(synthetic_home)))
    return ExecutableOrigin::User;

  struct stat status{};
  if (::stat(executable.canonical_path.c_str(), &status) == 0 && status.st_uid == geteuid())
    return ExecutableOrigin::User;
  return ExecutableOrigin::System;
}

ava::core::Result<std::optional<std::filesystem::path>> shebang_interpreter_path(std::filesystem::path const& executable, CommandLimits const& limits)
{
  std::ifstream input(executable, std::ios::binary);
  if (!input)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::Io, "failed to read executable for shebang inspection", "path", executable.string()));
  }
  std::string line;
  line.resize(limits.max_shebang_bytes + 1);
  input.read(line.data(), static_cast<std::streamsize>(line.size()));
  line.resize(static_cast<std::size_t>(input.gcount()));
  if (!line.starts_with("#!"))
    return std::optional<std::filesystem::path>{};
  auto const newline = line.find('\n');
  if (newline == std::string::npos && line.size() > limits.max_shebang_bytes)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "shebang line exceeds the bounded size", "path", executable.string()));
  }
  auto body = std::string_view(line).substr(2, newline == std::string::npos ? std::string_view::npos : newline - 2);
  if (!body.empty() && body.back() == '\r')
    body.remove_suffix(1);
  std::size_t first = 0;
  while (first < body.size() && std::isspace(static_cast<unsigned char>(body[first])) != 0) ++first;
  auto end = first;
  while (end < body.size() && std::isspace(static_cast<unsigned char>(body[end])) == 0) ++end;
  auto const interpreter = body.substr(first, end - first);
  if (interpreter.empty() || has_forbidden_byte(interpreter) || !std::filesystem::path(interpreter).is_absolute())
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "shebang must name an absolute interpreter path", "path", executable.string()));
  }
  return std::filesystem::path(interpreter);
}

ava::core::Result<std::vector<ExecutableMetadata>> inspect_shebang_chain(ExecutableMetadata const& executable, CommandLimits const& limits)
{
  std::vector<ExecutableMetadata> interpreters;
  std::set<std::filesystem::path> seen;
  seen.insert(executable.canonical_path);
  auto current = executable;
  while (true)
  {
    auto interpreter_path = shebang_interpreter_path(current.canonical_path, limits);
    if (!interpreter_path)
      return std::unexpected(std::move(interpreter_path.error()));
    if (!*interpreter_path)
      return interpreters;
    if (interpreters.size() == limits.max_shebang_depth)
    {
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "shebang interpreter chain exceeds the bounded depth", "path",
                                           executable.canonical_path.string()));
    }
    auto interpreter = executable_metadata(**interpreter_path);
    if (!interpreter)
      return std::unexpected(std::move(interpreter.error()));
    if (!seen.insert(interpreter->canonical_path).second)
    {
      return std::unexpected(
          command_error(ava::core::ErrorCategory::InvalidArgument, "shebang interpreter chain contains a cycle", "path", interpreter->canonical_path.string()));
    }
    interpreters.push_back(*interpreter);
    current = std::move(*interpreter);
  }
}

ava::core::Result<ResolvedExecutable> resolve_executable(std::vector<std::string> const& argv, std::vector<CommandPathEntry> const& path_entries,
                                                         std::filesystem::path const& cwd, std::filesystem::path const& workspace,
                                                         std::filesystem::path const& synthetic_home, CommandLimits const& limits)
{
  if (argv.empty() || argv.front().empty())
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "command argv has no executable identity"));

  std::filesystem::path requested(argv.front());
  ava::core::Result<ExecutableMetadata> metadata = std::unexpected(command_error(ava::core::ErrorCategory::NotFound, "executable was not found"));
  if (requested.is_absolute() || requested.string().find('/') != std::string::npos)
  {
    auto candidate = requested.is_absolute() ? requested : cwd / requested;
    metadata = executable_metadata(candidate);
  }
  else
  {
    for (auto const& entry : path_entries)
    {
      auto const candidate = entry.directory / requested;
      std::error_code exists_error;
      if (!std::filesystem::exists(candidate, exists_error) && !exists_error)
        continue;
      if (exists_error)
      {
        auto error = command_error(ava::core::ErrorCategory::Io, "failed to inspect executable candidate", "path", candidate.string());
        error.with_context("cause", exists_error.message());
        return std::unexpected(std::move(error));
      }
      metadata = executable_metadata(candidate);
      break;
    }
  }
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  auto interpreters = inspect_shebang_chain(*metadata, limits);
  if (!interpreters)
    return std::unexpected(std::move(interpreters.error()));
  return ResolvedExecutable{
      .executable = *metadata, .origin = executable_origin(*metadata, workspace, synthetic_home), .shebang_interpreters = std::move(*interpreters)};
}

bool is_safe_relative_argument(std::string_view value)
{
  if (value.empty() || has_forbidden_byte(value))
    return false;
  auto path = std::filesystem::path(value);
  if (path.is_absolute())
    return false;
  for (auto const& component : path.lexically_normal())
  {
    if (component == "..")
      return false;
  }
  return true;
}

bool is_simple_script_name(std::string_view value)
{
  if (value.empty() || value.size() > 128)
    return false;
  return std::ranges::all_of(value, [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == ':'; });
}

bool has_argument(std::vector<std::string> const& argv, std::initializer_list<std::string_view> candidates)
{
  return std::ranges::any_of(argv, [&candidates](std::string const& argument) {
    auto const lower = lowercase(argument);
    return std::ranges::find(candidates, std::string_view(lower)) != candidates.end();
  });
}

std::vector<std::string> canonical_recipe_argv(std::vector<std::string> const& argv, ResolvedExecutable const& executable)
{
  auto result = argv;
  result.front() = executable.executable.canonical_path.string();
  return result;
}

CommandClassification classify_raw_shell()
{
  return CommandClassification{.level = CommandLevel::Critical,
                               .family = CommandFamily::RawShell,
                               .recipe = std::nullopt,
                               .capabilities = CommandCapabilities{.raw_shell = true},
                               .max_interactive_scope = InteractiveScope::Once,
                               .ask_candidate = true};
}

CommandClassification critical_classification(CommandFamily family, CommandCapabilities capabilities)
{
  return CommandClassification{.level = CommandLevel::Critical,
                               .family = family,
                               .recipe = std::nullopt,
                               .capabilities = std::move(capabilities),
                               .max_interactive_scope = InteractiveScope::Once,
                               .ask_candidate = true};
}

CommandClassification sensitive_classification(CommandFamily family, CommandCapabilities capabilities)
{
  return CommandClassification{.level = CommandLevel::Sensitive,
                               .family = family,
                               .recipe = std::nullopt,
                               .capabilities = std::move(capabilities),
                               .max_interactive_scope = InteractiveScope::Workspace,
                               .ask_candidate = true};
}

CommandClassification standard_classification(CommandFamily family, CommandRecipe recipe, std::vector<std::string> canonical_argv,
                                              bool executes_mutable_project_code)
{
  return CommandClassification{.level = CommandLevel::Standard,
                               .family = family,
                               .recipe = RecipeDescriptor{.recipe = recipe, .canonical_argv = std::move(canonical_argv)},
                               .capabilities = CommandCapabilities{.executes_mutable_project_code = executes_mutable_project_code,
                                                                   .requires_containment = executes_mutable_project_code},
                               .max_interactive_scope = InteractiveScope::Workspace,
                               .ask_candidate = true};
}

bool is_inline_interpreter(std::string_view executable_name, std::vector<std::string> const& argv)
{
  static constexpr std::array<std::string_view, 12> kInterpreters{"bash", "sh",   "zsh",  "fish", "python", "python3",
                                                                  "node", "perl", "ruby", "php",  "lua",    "pwsh"};
  if (std::ranges::find(kInterpreters, executable_name) == kInterpreters.end())
    return false;
  return has_argument(argv, {"-c", "-e", "--command", "--eval"});
}

bool is_destructive_or_privileged(std::string_view executable_name, std::vector<std::string> const& argv)
{
  if (executable_name == "sudo" || executable_name == "doas" || executable_name == "pkexec" || executable_name == "rm" || executable_name == "dd" ||
      executable_name == "mkfs" || executable_name.starts_with("mkfs."))
  {
    return true;
  }
  if (has_argument(argv, {"--privileged", "--force-dangerous", "--hard", "--hard-reset"}))
    return true;
  if (executable_name == "git" && argv.size() >= 2)
  {
    auto const subcommand = lowercase(argv[1]);
    return subcommand == "clean" || (subcommand == "reset" && has_argument(argv, {"--hard"}));
  }
  return false;
}

std::optional<CommandClassification> classify_sensitive(std::string_view executable_name, std::vector<std::string> const& argv)
{
  auto const second = argv.size() >= 2 ? lowercase(argv[1]) : std::string{};
  if (executable_name == "git" &&
      (second == "push" || second == "pull" || second == "fetch" || second == "clone" || second == "remote" || second == "submodule"))
  {
    return sensitive_classification(CommandFamily::RemoteGitMutation, CommandCapabilities{.network_enabled = true});
  }
  if (executable_name == "git" && (second == "apply" || second == "checkout" || second == "restore" || second == "commit" || second == "merge" ||
                                   second == "rebase" || second == "cherry-pick"))
  {
    return sensitive_classification(CommandFamily::WorkspaceMutation, CommandCapabilities{.mutates_workspace = true});
  }
  if ((executable_name == "npm" || executable_name == "pnpm" || executable_name == "yarn" || executable_name == "bun" || executable_name == "pip" ||
       executable_name == "pip3" || executable_name == "cargo") &&
      (second == "install" || second == "update" || second == "upgrade" || second == "add" || second == "remove" || second == "uninstall"))
  {
    return sensitive_classification(CommandFamily::InstallOrUpdate, CommandCapabilities{.network_enabled = true, .mutates_workspace = true});
  }
  if (executable_name == "curl" || executable_name == "wget" || executable_name == "ssh" || executable_name == "scp" || executable_name == "rsync")
  {
    return sensitive_classification(CommandFamily::Network,
                                    CommandCapabilities{.network_enabled = true, .mutates_workspace = executable_name == "rsync" || executable_name == "scp"});
  }
  if (executable_name == "cmake" && (has_argument(argv, {"--install", "--install-prefix"})))
  {
    return sensitive_classification(CommandFamily::InstallOrUpdate, CommandCapabilities{.mutates_workspace = true});
  }
  if (has_argument(argv, {"publish", "deploy", "release"}))
  {
    return sensitive_classification(CommandFamily::PublishOrDeploy, CommandCapabilities{.network_enabled = true, .mutates_workspace = true});
  }
  if (executable_name == "touch" || executable_name == "mkdir" || executable_name == "cp" || executable_name == "mv" || executable_name == "chmod" ||
      executable_name == "patch" || executable_name == "tee" || (executable_name == "sed" && has_argument(argv, {"-i", "--in-place"})))
  {
    return sensitive_classification(CommandFamily::WorkspaceMutation, CommandCapabilities{.mutates_workspace = true});
  }
  return std::nullopt;
}

std::optional<CommandClassification> classify_standard(std::string_view executable_name, std::vector<std::string> const& argv,
                                                       ResolvedExecutable const& executable, std::filesystem::path const& workspace,
                                                       std::vector<WorkspaceScriptRecipe> const& workspace_recipes)
{
  auto canonical_argv = canonical_recipe_argv(argv, executable);
  if (executable_name == "pwd" && argv.size() == 1)
    return standard_classification(CommandFamily::Inspection, CommandRecipe::Pwd, std::move(canonical_argv), false);
  if (executable_name == "ls" && (argv.size() == 1 || (argv.size() == 2 && is_safe_relative_argument(argv[1]))))
    return standard_classification(CommandFamily::Inspection, CommandRecipe::Ls, std::move(canonical_argv), false);
  if (executable_name == "git" && argv.size() == 2 && lowercase(argv[1]) == "status")
    return standard_classification(CommandFamily::Inspection, CommandRecipe::GitStatus, std::move(canonical_argv), false);
  if (executable_name == "git" && argv.size() == 2 && lowercase(argv[1]) == "diff")
    return standard_classification(CommandFamily::Inspection, CommandRecipe::GitDiff, std::move(canonical_argv), false);
  if (executable_name == "git" && argv.size() == 3 && lowercase(argv[1]) == "log" && argv[2] == "-1")
    return standard_classification(CommandFamily::Inspection, CommandRecipe::GitLogOne, std::move(canonical_argv), false);

  if (executable_name == "cmake" && argv.size() == 3 && argv[1] == "--build" && is_safe_relative_argument(argv[2]))
    return standard_classification(CommandFamily::CmakeBuild, CommandRecipe::CmakeBuild, std::move(canonical_argv), true);
  if (executable_name == "ctest" && (argv.size() == 1 || (argv.size() == 3 && argv[1] == "--test-dir" && is_safe_relative_argument(argv[2]))))
    return standard_classification(CommandFamily::Ctest, CommandRecipe::Ctest, std::move(canonical_argv), true);
  if (executable_name == "ninja" && (argv.size() == 1 || (argv.size() == 3 && argv[1] == "-C" && is_safe_relative_argument(argv[2]))))
    return standard_classification(CommandFamily::Ninja, CommandRecipe::Ninja, std::move(canonical_argv), true);
  if (executable_name == "make" && (argv.size() == 1 || (argv.size() == 3 && argv[1] == "-C" && is_safe_relative_argument(argv[2]))))
    return standard_classification(CommandFamily::Make, CommandRecipe::Make, std::move(canonical_argv), true);
  if (executable_name == "cargo" && argv.size() == 2)
  {
    auto const verb = lowercase(argv[1]);
    if (verb == "build")
      return standard_classification(CommandFamily::Cargo, CommandRecipe::CargoBuild, std::move(canonical_argv), true);
    if (verb == "check")
      return standard_classification(CommandFamily::Cargo, CommandRecipe::CargoCheck, std::move(canonical_argv), true);
    if (verb == "test")
      return standard_classification(CommandFamily::Cargo, CommandRecipe::CargoTest, std::move(canonical_argv), true);
  }
  if ((executable_name == "npm" || executable_name == "pnpm" || executable_name == "yarn" || executable_name == "bun") && argv.size() == 3 &&
      lowercase(argv[1]) == "run" && is_simple_script_name(argv[2]))
  {
    return standard_classification(CommandFamily::PackageManagerScript, CommandRecipe::PackageManagerRunScript, std::move(canonical_argv), true);
  }
  if (executable_name == "pytest" && (argv.size() == 1 || (argv.size() == 2 && is_safe_relative_argument(argv[1]))))
    return standard_classification(CommandFamily::Pytest, CommandRecipe::Pytest, std::move(canonical_argv), true);

  for (auto const& configured_recipe : workspace_recipes)
  {
    auto script = configured_recipe.script.is_absolute() ? configured_recipe.script : workspace / configured_recipe.script;
    std::error_code canonical_error;
    auto canonical_script = std::filesystem::canonical(script, canonical_error);
    if (canonical_error || canonical_script != executable.executable.canonical_path || argv.size() != configured_recipe.argv_tail.size() + 1)
      continue;
    if (!std::ranges::equal(std::span(argv).subspan(1), configured_recipe.argv_tail))
      continue;
    return standard_classification(CommandFamily::WorkspaceScript, CommandRecipe::WorkspaceScript, std::move(canonical_argv), true);
  }
  return std::nullopt;
}

CommandClassification classify_command(std::vector<std::string> const& argv, ResolvedExecutable const& executable, std::filesystem::path const& workspace,
                                       std::vector<WorkspaceScriptRecipe> const& workspace_recipes)
{
  auto const executable_name = lowercase(executable.executable.canonical_path.filename().string());
  if (is_destructive_or_privileged(executable_name, argv))
  {
    return critical_classification(CommandFamily::DestructiveOrPrivileged, CommandCapabilities{.destructive_or_privileged = true});
  }
  if (is_inline_interpreter(executable_name, argv))
  {
    return critical_classification(CommandFamily::InterpreterInline, CommandCapabilities{.interpreter_inline = true});
  }
  if (auto sensitive = classify_sensitive(executable_name, argv))
    return *sensitive;
  if (auto standard = classify_standard(executable_name, argv, executable, workspace, workspace_recipes))
    return *standard;
  return critical_classification(CommandFamily::UnknownWrapper, CommandCapabilities{.unknown_wrapper = true});
}

class Fnv1a64
{
 public:
  void append_bytes(std::string_view value) noexcept
  {
    for (unsigned char const byte : value)
    {
      value_ ^= byte;
      value_ *= 1099511628211ULL;
    }
  }

  void append_field(std::string_view value) noexcept
  {
    std::array<char, sizeof(std::uint64_t)> length{};
    auto size = static_cast<std::uint64_t>(value.size());
    for (auto& byte : length)
    {
      byte = static_cast<char>(size & 0xffU);
      size >>= 8U;
    }
    append_bytes(std::string_view(length.data(), length.size()));
    append_bytes(value);
  }

  void append_number(std::uintmax_t value) noexcept { append_field(std::to_string(value)); }
  [[nodiscard]] std::string hex() const
  {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(16, '0');
    auto value = value_;
    for (std::size_t index = result.size(); index > 0; --index)
    {
      result[index - 1] = kHex[value & 0x0fU];
      value >>= 4U;
    }
    return result;
  }

 private:
  std::uint64_t value_ = 14695981039346656037ULL;
};

void append_metadata(Fnv1a64& hash, ExecutableMetadata const& metadata)
{
  hash.append_field(metadata.canonical_path.string());
  hash.append_number(metadata.device);
  hash.append_number(metadata.inode);
  hash.append_number(metadata.mode);
  hash.append_number(metadata.size);
  hash.append_field(std::to_string(metadata.modified_ticks));
}

std::string compute_fingerprint(CommandPlan const& plan)
{
  Fnv1a64 hash;
  hash.append_field("ava-command-plan-v1");
  hash.append_field(to_string(plan.intent_lane()));
  hash.append_field(to_string(plan.execution_domain()));
  hash.append_field(plan.workspace().string());
  hash.append_field(plan.cwd().string());
  hash.append_number(plan.argv().size());
  for (auto const& argument : plan.argv()) hash.append_field(argument);
  hash.append_field(plan.raw_shell_text());
  hash.append_number(plan.path_entries().size());
  for (auto const& entry : plan.path_entries())
  {
    hash.append_field(entry.directory.string());
    hash.append_field(to_string(entry.provenance));
  }
  hash.append_field(plan.resolved_executable() ? "resolved" : "raw");
  if (plan.resolved_executable())
  {
    append_metadata(hash, plan.resolved_executable()->executable);
    hash.append_field(to_string(plan.resolved_executable()->origin));
    hash.append_number(plan.resolved_executable()->shebang_interpreters.size());
    for (auto const& interpreter : plan.resolved_executable()->shebang_interpreters) append_metadata(hash, interpreter);
  }
  auto const& classification = plan.classification();
  hash.append_field(to_string(classification.level));
  hash.append_field(to_string(classification.family));
  hash.append_field(to_string(classification.max_interactive_scope));
  hash.append_field(classification.ask_candidate ? "1" : "0");
  hash.append_field(classification.capabilities.executes_mutable_project_code ? "1" : "0");
  hash.append_field(classification.capabilities.requires_containment ? "1" : "0");
  hash.append_field(classification.capabilities.network_enabled ? "1" : "0");
  hash.append_field(classification.capabilities.mutates_workspace ? "1" : "0");
  hash.append_field(classification.capabilities.destructive_or_privileged ? "1" : "0");
  hash.append_field(classification.capabilities.interpreter_inline ? "1" : "0");
  hash.append_field(classification.capabilities.unknown_wrapper ? "1" : "0");
  hash.append_field(classification.capabilities.raw_shell ? "1" : "0");
  hash.append_field(classification.recipe ? to_string(classification.recipe->recipe) : "none");
  if (classification.recipe)
  {
    hash.append_number(classification.recipe->canonical_argv.size());
    for (auto const& argument : classification.recipe->canonical_argv) hash.append_field(argument);
  }
  // The environment profile identifier is intentionally the only environment
  // data admitted to the sealed plan and its fingerprint.
  hash.append_field(plan.environment_profile_id());
  return hash.hex();
}

std::string json_escape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (unsigned char const ch : value)
  {
    switch (ch)
    {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (ch < 0x20)
        {
          static constexpr char kHex[] = "0123456789abcdef";
          escaped += "\\u00";
          escaped.push_back(kHex[(ch >> 4U) & 0x0fU]);
          escaped.push_back(kHex[ch & 0x0fU]);
        }
        else
        {
          escaped.push_back(static_cast<char>(ch));
        }
    }
  }
  return escaped;
}

bool metadata_is_fresh(ExecutableMetadata const& recorded)
{
  auto current = executable_metadata(recorded.canonical_path);
  return current && *current == recorded;
}

ava::core::VoidResult validate_environment_path(std::filesystem::path const& path, std::string_view name, CommandLimits const& limits)
{
  if (path.empty() || !path.is_absolute() || has_forbidden_path_byte(path) || path.string().size() > limits.max_path_bytes)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument,
                                         std::string(name) + " must be a bounded absolute synthetic environment path", "path", path.string()));
  }
  return {};
}

std::string join_path(std::vector<CommandPathEntry> const& entries)
{
  std::string result;
  for (auto const& entry : entries)
  {
    if (!result.empty())
      result.push_back(':');
    result += entry.directory.string();
  }
  return result;
}

}  // namespace

CommandIntent::CommandIntent(CommandIntentLane lane, std::vector<std::string> argv, std::optional<std::filesystem::path> cwd, std::string source_text)
    : lane_(lane), argv_(std::move(argv)), cwd_(std::move(cwd)), source_text_(std::move(source_text))
{
}

ava::core::Result<CommandIntent> CommandIntent::compatibility(std::string command, CommandLimits limits)
{
  auto parsed = parse_compatibility_command(command, limits);
  if (!parsed)
    return std::unexpected(std::move(parsed.error()));
  if (parsed->has_unquoted_shell_metacharacter)
    return CommandIntent(CommandIntentLane::RawShell, {}, std::nullopt, std::move(command));
  return CommandIntent(CommandIntentLane::Compatibility, std::move(parsed->argv), std::nullopt, std::move(command));
}

ava::core::Result<CommandIntent> CommandIntent::structured(std::vector<std::string> argv, std::optional<std::filesystem::path> cwd, CommandLimits limits)
{
  if (auto valid = validate_structured_argv(argv, limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (cwd && (cwd->empty() || has_forbidden_path_byte(*cwd) || cwd->string().size() > limits.max_path_bytes))
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::InvalidArgument, "structured command cwd must be non-empty, safe, and within its bound", "cwd", cwd->string()));
  }
  return CommandIntent(CommandIntentLane::StructuredArgv, std::move(argv), std::move(cwd), {});
}

ava::core::Result<CommandIntent> CommandIntent::raw_shell(std::string shell_text, CommandLimits limits)
{
  if (auto valid = validate_raw_shell(shell_text, limits); !valid)
    return std::unexpected(std::move(valid.error()));
  return CommandIntent(CommandIntentLane::RawShell, {}, std::nullopt, std::move(shell_text));
}

CommandIntentLane CommandIntent::lane() const noexcept
{
  return lane_;
}

bool CommandIntent::has_argv() const noexcept
{
  return lane_ != CommandIntentLane::RawShell;
}

std::vector<std::string> const& CommandIntent::argv() const noexcept
{
  return argv_;
}

std::optional<std::filesystem::path> const& CommandIntent::requested_cwd() const noexcept
{
  return cwd_;
}

std::string_view CommandIntent::source_text() const noexcept
{
  return source_text_;
}

std::string const& CommandEnvironment::profile_id() const noexcept
{
  return profile_id_;
}

std::vector<EnvironmentVariable> const& CommandEnvironment::entries() const noexcept
{
  return entries_;
}

ava::core::Result<CommandEnvironment> build_command_environment(CommandEnvironmentOptions const& options, std::vector<CommandPathEntry> const& path_entries,
                                                                CommandLimits const& limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (options.profile_id.empty() || options.profile_id.size() > 128 || has_forbidden_byte(options.profile_id) ||
      !std::ranges::all_of(options.profile_id, [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.'; }))
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "environment profile id must be a bounded safe identifier"));
  }
  for (auto const* value : {&options.user, &options.logname})
  {
    if (value->empty() || value->size() > limits.max_argument_bytes || has_forbidden_byte(*value))
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "USER and LOGNAME must be bounded safe values"));
  }
  for (auto const& [name, path] : std::array<std::pair<std::string_view, std::filesystem::path const*>, 6>{{{"HOME", &options.home},
                                                                                                            {"XDG_CONFIG_HOME", &options.xdg_config_home},
                                                                                                            {"XDG_CACHE_HOME", &options.xdg_cache_home},
                                                                                                            {"XDG_DATA_HOME", &options.xdg_data_home},
                                                                                                            {"XDG_STATE_HOME", &options.xdg_state_home},
                                                                                                            {"TMPDIR", &options.tmpdir}}})
  {
    if (auto valid = validate_environment_path(*path, name, limits); !valid)
      return std::unexpected(std::move(valid.error()));
  }

  auto const path = join_path(path_entries);
  if (path.size() > limits.max_path_bytes || has_forbidden_byte(path))
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "derived PATH exceeds its bounded safe representation"));

  CommandEnvironment environment;
  environment.profile_id_ = options.profile_id;
  environment.entries_ = {{"LANG", "C.UTF-8"},
                          {"LC_ALL", "C.UTF-8"},
                          {"LC_CTYPE", "C.UTF-8"},
                          {"TZ", "UTC"},
                          {"USER", options.user},
                          {"LOGNAME", options.logname},
                          {"PATH", path},
                          {"HOME", options.home.string()},
                          {"XDG_CONFIG_HOME", options.xdg_config_home.string()},
                          {"XDG_CACHE_HOME", options.xdg_cache_home.string()},
                          {"XDG_DATA_HOME", options.xdg_data_home.string()},
                          {"XDG_STATE_HOME", options.xdg_state_home.string()},
                          {"TMPDIR", options.tmpdir.string()}};
  return environment;
}

ava::core::Result<CommandPlan> seal_command_plan(CommandIntent const& intent, CommandBuildOptions const& options)
{
  if (auto valid = validate_limits(options.limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (intent.lane() == CommandIntentLane::RawShell)
  {
    if (auto valid = validate_raw_shell(intent.source_text(), options.limits); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  else if (auto valid = validate_structured_argv(intent.argv(), options.limits); !valid)
  {
    return std::unexpected(std::move(valid.error()));
  }

  auto workspace = canonical_workspace(options.workspace);
  if (!workspace)
    return std::unexpected(std::move(workspace.error()));
  auto cwd = canonical_cwd(intent, *workspace);
  if (!cwd)
    return std::unexpected(std::move(cwd.error()));
  auto path_entries = discover_path(options, *workspace);
  if (!path_entries)
    return std::unexpected(std::move(path_entries.error()));
  auto environment = build_command_environment(options.environment, *path_entries, options.limits);
  if (!environment)
    return std::unexpected(std::move(environment.error()));

  CommandPlan plan;
  plan.intent_lane_ = intent.lane();
  plan.workspace_ = std::move(*workspace);
  plan.cwd_ = std::move(*cwd);
  plan.path_entries_ = std::move(*path_entries);
  plan.environment_profile_id_ = environment->profile_id();
  if (intent.lane() == CommandIntentLane::RawShell)
  {
    plan.execution_domain_ = CommandExecutionDomain::RawShell;
    plan.raw_shell_text_ = std::string(intent.source_text());
    plan.classification_ = classify_raw_shell();
  }
  else
  {
    plan.execution_domain_ = CommandExecutionDomain::DirectArgv;
    plan.argv_ = intent.argv();
    auto resolved = resolve_executable(plan.argv_, plan.path_entries_, plan.cwd_, plan.workspace_, options.environment.home, options.limits);
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    plan.classification_ = classify_command(plan.argv_, *resolved, plan.workspace_, options.workspace_script_recipes);
    plan.resolved_executable_ = std::move(*resolved);
  }
  plan.fingerprint_ = compute_fingerprint(plan);
  return plan;
}

ava::core::Result<CommandPreparation> prepare_command(CommandIntent const& intent, CommandBuildOptions const& options)
{
  auto plan = seal_command_plan(intent, options);
  if (!plan)
    return std::unexpected(std::move(plan.error()));
  auto environment = build_command_environment(options.environment, plan->path_entries(), options.limits);
  if (!environment)
    return std::unexpected(std::move(environment.error()));
  return CommandPreparation{.plan = std::move(*plan), .environment = std::move(*environment)};
}

CommandIntentLane CommandPlan::intent_lane() const noexcept
{
  return intent_lane_;
}

CommandExecutionDomain CommandPlan::execution_domain() const noexcept
{
  return execution_domain_;
}

std::filesystem::path const& CommandPlan::workspace() const noexcept
{
  return workspace_;
}

std::filesystem::path const& CommandPlan::cwd() const noexcept
{
  return cwd_;
}

std::vector<std::string> const& CommandPlan::argv() const noexcept
{
  return argv_;
}

std::string const& CommandPlan::raw_shell_text() const noexcept
{
  return raw_shell_text_;
}

std::vector<CommandPathEntry> const& CommandPlan::path_entries() const noexcept
{
  return path_entries_;
}

std::optional<ResolvedExecutable> const& CommandPlan::resolved_executable() const noexcept
{
  return resolved_executable_;
}

CommandClassification const& CommandPlan::classification() const noexcept
{
  return classification_;
}

std::string const& CommandPlan::environment_profile_id() const noexcept
{
  return environment_profile_id_;
}

std::string const& CommandPlan::fingerprint() const noexcept
{
  return fingerprint_;
}

std::string CommandPlan::display_json() const
{
  std::ostringstream output;
  output << "{\"fingerprint\":\"" << json_escape(fingerprint_) << "\",\"intent_lane\":\"" << to_string(intent_lane_) << "\",\"execution_domain\":\""
         << to_string(execution_domain_) << "\",\"workspace\":\"" << json_escape(workspace_.string()) << "\",\"cwd\":\"" << json_escape(cwd_.string())
         << "\",\"environment_profile_id\":\"" << json_escape(environment_profile_id_) << "\",\"level\":\"" << to_string(classification_.level)
         << "\",\"family\":\"" << to_string(classification_.family) << "\",\"argv\":[";
  for (std::size_t index = 0; index < argv_.size(); ++index)
  {
    if (index != 0)
      output << ',';
    output << '"' << json_escape(argv_[index]) << '"';
  }
  output << "]";
  if (!raw_shell_text_.empty())
    output << ",\"raw_shell\":\"" << json_escape(raw_shell_text_) << '"';
  output << '}';
  return std::move(output).str();
}

ava::core::Result<bool> plan_is_fresh(CommandPlan const& plan)
{
  if (!plan.resolved_executable())
    return true;
  if (!metadata_is_fresh(plan.resolved_executable()->executable))
    return false;
  for (auto const& interpreter : plan.resolved_executable()->shebang_interpreters)
  {
    if (!metadata_is_fresh(interpreter))
      return false;
  }
  return true;
}

bool command_mode_is_enabled(CommandRuntimeOptions const& options) noexcept
{
  return options.mode == CommandRuntimeMode::Enabled;
}

bool command_mode_is_prompt_only(CommandRuntimeOptions const& options) noexcept
{
  return options.mode == CommandRuntimeMode::PromptOnly;
}

std::string_view to_string(CommandIntentLane value) noexcept
{
  switch (value)
  {
    case CommandIntentLane::Compatibility:
      return "compatibility";
    case CommandIntentLane::StructuredArgv:
      return "structured_argv";
    case CommandIntentLane::RawShell:
      return "raw_shell";
  }
  return "unknown";
}

std::string_view to_string(CommandRuntimeMode value) noexcept
{
  switch (value)
  {
    case CommandRuntimeMode::Legacy:
      return "legacy";
    case CommandRuntimeMode::PromptOnly:
      return "prompt-only";
    case CommandRuntimeMode::Enabled:
      return "enabled";
  }
  return "legacy";
}

std::string_view to_string(CommandExecutionDomain value) noexcept
{
  switch (value)
  {
    case CommandExecutionDomain::DirectArgv:
      return "direct_argv";
    case CommandExecutionDomain::RawShell:
      return "raw_shell";
  }
  return "unknown";
}

std::string_view to_string(PathProvenance value) noexcept
{
  switch (value)
  {
    case PathProvenance::StartupPath:
      return "startup_path";
    case PathProvenance::UserLocal:
      return "user_local";
    case PathProvenance::UserCargo:
      return "user_cargo";
    case PathProvenance::WorkspaceVenv:
      return "workspace_venv";
    case PathProvenance::WorkspaceNodeModules:
      return "workspace_node_modules";
  }
  return "unknown";
}

std::string_view to_string(ExecutableOrigin value) noexcept
{
  switch (value)
  {
    case ExecutableOrigin::System:
      return "system";
    case ExecutableOrigin::User:
      return "user";
    case ExecutableOrigin::Workspace:
      return "workspace";
  }
  return "unknown";
}

std::string_view to_string(CommandLevel value) noexcept
{
  switch (value)
  {
    case CommandLevel::Standard:
      return "standard";
    case CommandLevel::Sensitive:
      return "sensitive";
    case CommandLevel::Critical:
      return "critical";
  }
  return "critical";
}

std::string_view to_string(CommandFamily value) noexcept
{
  switch (value)
  {
    case CommandFamily::Inspection:
      return "inspection";
    case CommandFamily::CmakeBuild:
      return "cmake_build";
    case CommandFamily::Ctest:
      return "ctest";
    case CommandFamily::Ninja:
      return "ninja";
    case CommandFamily::Make:
      return "make";
    case CommandFamily::Cargo:
      return "cargo";
    case CommandFamily::PackageManagerScript:
      return "package_manager_script";
    case CommandFamily::Pytest:
      return "pytest";
    case CommandFamily::WorkspaceScript:
      return "workspace_script";
    case CommandFamily::InstallOrUpdate:
      return "install_or_update";
    case CommandFamily::RemoteGitMutation:
      return "remote_git_mutation";
    case CommandFamily::PublishOrDeploy:
      return "publish_or_deploy";
    case CommandFamily::Network:
      return "network";
    case CommandFamily::WorkspaceMutation:
      return "workspace_mutation";
    case CommandFamily::InterpreterInline:
      return "interpreter_inline";
    case CommandFamily::DestructiveOrPrivileged:
      return "destructive_or_privileged";
    case CommandFamily::UnknownWrapper:
      return "unknown_wrapper";
    case CommandFamily::RawShell:
      return "raw_shell";
  }
  return "unknown_wrapper";
}

std::string_view to_string(CommandRecipe value) noexcept
{
  switch (value)
  {
    case CommandRecipe::Pwd:
      return "pwd";
    case CommandRecipe::Ls:
      return "ls";
    case CommandRecipe::GitStatus:
      return "git_status";
    case CommandRecipe::GitDiff:
      return "git_diff";
    case CommandRecipe::GitLogOne:
      return "git_log_one";
    case CommandRecipe::CmakeBuild:
      return "cmake_build";
    case CommandRecipe::Ctest:
      return "ctest";
    case CommandRecipe::Ninja:
      return "ninja";
    case CommandRecipe::Make:
      return "make";
    case CommandRecipe::CargoBuild:
      return "cargo_build";
    case CommandRecipe::CargoCheck:
      return "cargo_check";
    case CommandRecipe::CargoTest:
      return "cargo_test";
    case CommandRecipe::PackageManagerRunScript:
      return "package_manager_run_script";
    case CommandRecipe::Pytest:
      return "pytest";
    case CommandRecipe::WorkspaceScript:
      return "workspace_script";
  }
  return "unknown";
}

std::string_view to_string(InteractiveScope value) noexcept
{
  switch (value)
  {
    case InteractiveScope::Once:
      return "once";
    case InteractiveScope::Workspace:
      return "workspace";
  }
  return "once";
}

}  // namespace ava::command
