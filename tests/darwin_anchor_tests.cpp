#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/ava_debug.h"
#include "ava/core/AnchorOpen.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/open_beneath.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include "debug.h"

namespace {
enum class Fault
{
  None,
  PathFailure,
  RelativePath,
  Mislocation,
  ProofFailure
};
Fault fault = Fault::None;
std::string fault_path;
std::string claimed_path;
std::function<void()> before_final_open;
bool forbid_reverse_parent = false;
unsigned int reverse_parent_calls = 0;
unsigned int fault_hits = 0;
}  // namespace

// Compile the exact production sources below with syscall observation only in
// this test executable. No fault hook or global test state enters ava_core.
int observed_openat(int directory, char const* path, int flags, ...)
{
  mode_t mode = 0;
  if ((flags & O_CREAT) != 0)
  {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  if (forbid_reverse_parent && std::strcmp(path, "..") == 0)
  {
    ++reverse_parent_calls;
    errno = ENOTDIR;
    return -1;
  }
  if (fault == Fault::ProofFailure && (flags & O_RESOLVE_BENEATH) != 0 && std::strcmp(path, "subdir") == 0)
  {
    ++fault_hits;
    errno = EIO;
    return -1;
  }
  if (before_final_open && (flags & O_RESOLVE_BENEATH) != 0 && std::strcmp(path, "race-target") == 0)
  {
    auto hook = std::move(before_final_open);
    before_final_open = {};
    ++fault_hits;
    hook();
  }
  return (flags & O_CREAT) != 0 ? ::openat(directory, path, flags, mode) : ::openat(directory, path, flags);
}

int observed_fcntl(int fd, int command, ...)
{
  if (command == F_GETPATH_NOFIRMLINK)
  {
    va_list args;
    va_start(args, command);
    auto* buffer = va_arg(args, char*);
    va_end(args);
    int const result = ::fcntl(fd, command, buffer);
    if (result == 0 && (fault_path.empty() || fault_path == buffer))
    {
      if (fault == Fault::PathFailure)
      {
        ++fault_hits;
        errno = EIO;
        return -1;
      }
      if (fault == Fault::RelativePath || fault == Fault::Mislocation)
      {
        ++fault_hits;
        std::snprintf(buffer, PATH_MAX, "%s", fault == Fault::RelativePath ? "ambiguous-relative-path" : claimed_path.c_str());
      }
    }
    return result;
  }
  if (command == F_DUPFD_CLOEXEC || command == F_SETFL)
  {
    va_list args;
    va_start(args, command);
    int const value = va_arg(args, int);
    va_end(args);
    return ::fcntl(fd, command, value);
  }
  return ::fcntl(fd, command);
}

#define openat observed_openat
#define fcntl observed_fcntl
#include "../src/ava/core/AnchorOpen.cpp"
#include "../src/ava/core/open_beneath.cpp"
#undef fcntl
#undef openat

namespace {
void check_denied(ava::core::Result<ava::core::AnchorOpen> const& result, char const* label)
{
  expect(!result && result.error().category() == ava::core::ErrorCategory::PermissionDenied, label);
}

void contract()
{
  namespace fs = std::filesystem;
  auto const root = create_empty_root("darwin-forward-anchor");
  auto const anchor_path = root / "anchor";
  auto const external = root / "external";
  fs::create_directories(anchor_path / "subdir");
  fs::create_directories(external / "nested");
  fs::create_directories(root / "anchor-other");
  std::ofstream(anchor_path / "inside") << "inside";
  std::ofstream(external / "ordinary") << "external";
  std::ofstream(root / "anchor-other" / "ordinary") << "external sibling";
  fs::create_symlink("inside", anchor_path / "contained");
  fs::create_symlink("../external/ordinary", anchor_path / "escape");
  fs::create_symlink(external / "ordinary", anchor_path / "absolute-escape");
  fs::create_directory_symlink(anchor_path, external / "to-anchor");
  fs::create_directory_symlink(anchor_path / "subdir", external / "to-subdir");
  fs::create_symlink("../ordinary", external / "nested" / "up");
  std::string past_root;
  for (int depth = 0; depth < 64; ++depth)
    past_root += "../";
  fs::create_symlink(past_root + "usr/bin", external / "nested" / "past-root");
  auto anchors = ava::core::AnchorSet::open({anchor_path});
  expect(anchors.has_value(), "Darwin anchor fixture opens");
  if (!anchors)
    return;
  auto const& set = **anchors;
  auto const& anchor = set.anchors().front();

  expect(ava::core::open_readable(set, anchor_path / "inside", O_RDONLY).has_value(), "native beneath opens ordinary in-anchor file");
  expect(ava::core::open_readable(set, anchor_path / "contained", O_RDONLY).has_value(), "native beneath follows contained symlink");
  check_denied(ava::core::open_readable(set, anchor_path / "escape", O_RDONLY), "native beneath rejects escaping symlink as PermissionDenied");
  check_denied(ava::core::open_readable(set, anchor_path / "absolute-escape", O_RDONLY), "native beneath rejects absolute symlink as PermissionDenied");
  auto created = ava::core::open_writable(set, anchor_path / "created", O_WRONLY | O_CREAT | O_EXCL, 0600);
  struct stat created_status{};
  expect(created && ::fstat(created->fd(), &created_status) == 0 && (created_status.st_mode & 0777) == 0600, "native beneath preserves O_CREAT mode");
  errno = 0;
  int const escaped = ava::core::open_beneath(anchor.fd, "../external/ordinary", O_RDONLY);
  int const escape_errno = errno;
  expect(escaped == -1 && ava::core::open_path_error("escape", escape_errno, {}).category() == ava::core::ErrorCategory::PermissionDenied,
         "explicit dotdot escape maps to PermissionDenied");
  if (escaped >= 0)
    ::close(escaped);

  forbid_reverse_parent = true;
  for (auto const& path : {external / "ordinary", root / "anchor-other" / "ordinary", external / "nested" / "up", fs::path("/usr/bin")})
    expect(ava::core::open_readable(set, path, O_RDONLY).has_value(),
           "forward external traversal opens ordinary, component-sibling, dotdot-link and system cases");
  expect(!ava::core::open_readable(set, external / "to-anchor", O_RDONLY), "external alias to anchor rejected");
  expect(!ava::core::open_readable(set, external / "to-subdir", O_RDONLY), "external alias to anchor subdirectory rejected");
  expect(ava::core::open_readable(set, external / "nested" / "past-root", O_RDONLY).has_value(), "directory stack clamps dotdot at held root");

  // Exercise descendant proof directly, without first visiting the anchor root.
  ava::core::UniqueFd held(::open((anchor_path / "subdir").c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  std::vector<ava::core::PhysicalAnchor> physical{{ava::core::file_id(anchor.fd), anchor.fd, ava::core::physical_directory_path(anchor.fd)}};
  expect(ava::core::is_beneath_any_anchor(held.get(), physical), "forward proof identifies held subdirectory without walking anchor first");
  std::array<char, PATH_MAX> logical_kernel_path{};
  expect(::fcntl(anchor.fd, F_GETPATH, logical_kernel_path.data()) == 0 && physical.front().path != fs::path(logical_kernel_path.data()),
         "temporary anchor fixture crosses Darwin Data-volume firmlink namespace");

  fault = Fault::ProofFailure;
  bool proof_failed_closed = false;
  try
  {
    static_cast<void>(ava::core::is_beneath_any_anchor(held.get(), physical));
  }
  catch (std::system_error const&)
  {
    proof_failed_closed = true;
  }
  expect(proof_failed_closed && fault_hits != 0, "apparent descendant anchored reopen failure is not external");
  fault = Fault::None;

  ava::core::UniqueFd outside(::open(external.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  fault_path = ava::core::physical_directory_path(outside.get()).string();
  claimed_path = (physical.front().path / "subdir").string();
  fault = Fault::Mislocation;
  bool identity_failed_closed = false;
  try
  {
    static_cast<void>(ava::core::is_beneath_any_anchor(outside.get(), physical));
  }
  catch (std::system_error const&)
  {
    identity_failed_closed = true;
  }
  expect(identity_failed_closed, "physical path hint never overrides mismatching held-object identity");
  fault = Fault::None;
  auto const outside_physical = ava::core::physical_directory_path(outside.get()).string();
  for (auto const injected : {Fault::PathFailure, Fault::RelativePath})
  {
    fault_path = outside_physical;
    fault = injected;
    expect(!ava::core::open_readable(set, external / "ordinary", O_RDONLY), "held directory no-firmlink path failure/ambiguity fails closed");
    fault_path.clear();
    expect(!ava::core::open_readable(set, external / "ordinary", O_RDONLY), "anchor no-firmlink path failure/ambiguity fails closed");
  }
  fault = Fault::None;
  fault_path.clear();

  auto const race_target = external / "race-target";
  std::ofstream(race_target) << "original";
  before_final_open = [&] {
    fs::rename(race_target, external / "retained-original");
    std::ofstream(race_target) << "replacement";
  };
  auto raced = ava::core::open_readable(set, race_target, O_RDONLY);
  expect(!before_final_open && !raced && raced.error().category() == ava::core::ErrorCategory::Io,
         "parent-relative final reopen rejects deterministic external object replacement" +
             (raced ? std::string(" (unexpected success)") : ": " + raced.error().format()));
  expect(reverse_parent_calls == 0, "external ancestry and relative symlink resolution never call reverse openat dotdot");
}

void cryptex()
{
  auto const root = create_empty_root("darwin-cryptex-forward");
  auto anchors = ava::core::AnchorSet::open({root});
  expect(anchors.has_value(), "Cryptex fixture anchor opens");
  if (!anchors)
    return;
  forbid_reverse_parent = true;
  for (int iteration = 0; iteration < 1000; ++iteration)
  {
    auto result = ava::core::open_readable(**anchors, "/System/Cryptexes/App/usr/bin", O_RDONLY | O_DIRECTORY);
    expect(result.has_value(), "Cryptex forward external read iteration " + std::to_string(iteration));
  }
  expect(reverse_parent_calls == 0, "1000 Cryptex traversals never use reverse openat dotdot");
}
}  // namespace

int main(int argc, char** argv)
{
#ifdef CWDEBUG
  Debug(ava::app::debug_init(false));
#endif
  if (argc == 2 && std::string_view(argv[1]) == "cryptex")
    cryptex();
  else
    contract();
  std::printf("Darwin forward tests: failures=%d reverse_parent_calls=%u fault_hits=%u\n", ava::tests::failures(), reverse_parent_calls, fault_hits);
  return ava::tests::failures() == 0 ? 0 : 1;
}
