#include "sys.h"
#include "ava/command/discovery.h"
#include "ava/command/environment.h"
#include "ava/command/intent_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ava::command::detail {
namespace {

std::uint32_t rotr(std::uint32_t value, std::uint32_t count)
{
  return (value >> count) | (value << (32U - count));
}

std::array<std::uint8_t, 32> sha256(std::vector<std::uint8_t> const& bytes)
{
  // Keep this focused SHA-256 implementation private to sealed-plan
  // fingerprints and environment bindings; it is not a general crypto API.
  static constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU,
      0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU,
      0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U,
      0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
      0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
      0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

  std::vector<std::uint8_t> data = bytes;
  auto const bit_length = static_cast<std::uint64_t>(data.size()) * 8ULL;
  data.push_back(0x80U);
  while ((data.size() % 64U) != 56U) data.push_back(0U);
  for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));

  std::array<std::uint32_t, 8> hash{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  for (std::size_t chunk = 0; chunk < data.size(); chunk += 64U)
  {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16U; ++index)
    {
      auto const offset = chunk + index * 4U;
      words[index] = (static_cast<std::uint32_t>(data[offset]) << 24U) | (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) | static_cast<std::uint32_t>(data[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index)
    {
      auto const s0 = rotr(words[index - 15U], 7U) ^ rotr(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
      auto const s1 = rotr(words[index - 2U], 17U) ^ rotr(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }
    auto a = hash[0];
    auto b = hash[1];
    auto c = hash[2];
    auto d = hash[3];
    auto e = hash[4];
    auto f = hash[5];
    auto g = hash[6];
    auto h = hash[7];
    for (std::size_t index = 0; index < words.size(); ++index)
    {
      auto const s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
      auto const choose = (e & f) ^ ((~e) & g);
      auto const temporary_one = h + s1 + choose + constants[index] + words[index];
      auto const s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
      auto const majority = (a & b) ^ (a & c) ^ (b & c);
      auto const temporary_two = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary_one;
      d = c;
      c = b;
      b = a;
      a = temporary_one + temporary_two;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::array<std::uint8_t, 32> result{};
  for (std::size_t index = 0; index < hash.size(); ++index)
  {
    result[index * 4U] = static_cast<std::uint8_t>((hash[index] >> 24U) & 0xffU);
    result[index * 4U + 1U] = static_cast<std::uint8_t>((hash[index] >> 16U) & 0xffU);
    result[index * 4U + 2U] = static_cast<std::uint8_t>((hash[index] >> 8U) & 0xffU);
    result[index * 4U + 3U] = static_cast<std::uint8_t>(hash[index] & 0xffU);
  }
  return result;
}

ava::core::VoidResult validate_sealed_environment_root(PathMetadata const& root, std::string_view name, CommandLimits const& limits)
{
  if (root.canonical_path.empty() || !root.canonical_path.is_absolute() || has_forbidden_path_byte(root.canonical_path) ||
      root.canonical_path.string().size() > limits.max_path_bytes)
  {
    return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument,
                                         std::string(name) + " is not a bounded canonical synthetic environment path", "path", root.canonical_path.string()));
  }
  auto fresh = path_metadata_is_fresh(root);
  if (!fresh)
    return std::unexpected(std::move(fresh.error()));
  if (!*fresh)
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::Io, std::string(name) + " changed during environment construction", "path", root.requested_path.string()));
  }
  return {};
}

ava::core::Result<std::string> join_path(std::vector<CommandPathEntry> const& entries, CommandLimits const& limits)
{
  std::string result;
  for (auto const& entry : entries)
  {
    // Environment construction is internal, but retain this validation so no
    // future caller can inject a second PATH entry through a colon-containing
    // CommandPathEntry.
    if (entry.directory.empty() || !entry.directory.is_absolute() || entry.directory.string().find(':') != std::string::npos ||
        has_forbidden_path_byte(entry.directory) || entry.metadata.canonical_path != entry.directory)
    {
      return std::unexpected(
          command_error(ava::core::ErrorCategory::InvalidArgument, "sealed PATH entry is not safely representable", "path", entry.directory.string()));
    }
    auto fresh = path_metadata_is_fresh(entry.metadata);
    if (!fresh)
      return std::unexpected(std::move(fresh.error()));
    if (!*fresh)
      return std::unexpected(
          command_error(ava::core::ErrorCategory::Io, "sealed PATH entry changed during environment construction", "path", entry.directory.string()));
    std::size_t const separator = result.empty() ? 0U : 1U;
    if (result.size() > limits.max_path_bytes || separator > limits.max_path_bytes - result.size() ||
        entry.directory.string().size() > limits.max_path_bytes - result.size() - separator)
    {
      return std::unexpected(command_error(ava::core::ErrorCategory::InvalidArgument, "derived PATH exceeds the bounded safe representation"));
    }
    if (separator != 0)
      result.push_back(':');
    result += entry.directory.string();
  }
  return result;
}

std::string environment_digest(std::string_view profile_id, std::vector<EnvironmentVariable> const& entries)
{
  Sha256Builder hash;
  hash.append_field("ava-command-environment-v1");
  hash.append_field(profile_id);
  hash.append_number(entries.size());
  for (auto const& entry : entries)
  {
    hash.append_field(entry.key);
    hash.append_field(entry.value);
  }
  return "sha256:ava-command-environment-v1:" + hash.hex();
}

}  // namespace

void Sha256Builder::append_bytes(std::string_view value)
{
  bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void Sha256Builder::append_field(std::string_view value)
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

void Sha256Builder::append_number(std::uintmax_t value)
{
  append_field(std::to_string(value));
}

std::string Sha256Builder::hex() const
{
  auto const digest = sha256(bytes_);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (auto const byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
  return std::move(output).str();
}

ava::core::Result<CommandEnvironment> EnvironmentFactory::make(CommandEnvironmentOptions const& options, std::vector<CommandPathEntry> const& path_entries,
                                                               SyntheticEnvironmentRoots roots, CommandLimits const& limits,
                                                               CommandEnvironment::FactoryPasskey passkey)
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
  for (auto const& [name, root] : std::array<std::pair<std::string_view, PathMetadata const*>, 6>{{{"HOME", &roots.home},
                                                                                                   {"XDG_CONFIG_HOME", &roots.xdg_config_home},
                                                                                                   {"XDG_CACHE_HOME", &roots.xdg_cache_home},
                                                                                                   {"XDG_DATA_HOME", &roots.xdg_data_home},
                                                                                                   {"XDG_STATE_HOME", &roots.xdg_state_home},
                                                                                                   {"TMPDIR", &roots.tmpdir}}})
  {
    if (auto valid = validate_sealed_environment_root(*root, name, limits); !valid)
      return std::unexpected(std::move(valid.error()));
  }

  auto path = join_path(path_entries, limits);
  if (!path)
    return std::unexpected(std::move(path.error()));
  CommandEnvironment environment(std::move(passkey), std::move(roots));
  environment.profile_id_ = options.profile_id;
  environment.entries_ = {{"LANG", "C.UTF-8"},
                          {"LC_ALL", "C.UTF-8"},
                          {"LC_CTYPE", "C.UTF-8"},
                          {"TZ", "UTC"},
                          {"USER", options.user},
                          {"LOGNAME", options.logname},
                          {"PATH", std::move(*path)},
                          {"HOME", environment.roots_.home.canonical_path.string()},
                          {"XDG_CONFIG_HOME", environment.roots_.xdg_config_home.canonical_path.string()},
                          {"XDG_CACHE_HOME", environment.roots_.xdg_cache_home.canonical_path.string()},
                          {"XDG_DATA_HOME", environment.roots_.xdg_data_home.canonical_path.string()},
                          {"XDG_STATE_HOME", environment.roots_.xdg_state_home.canonical_path.string()},
                          {"TMPDIR", environment.roots_.tmpdir.canonical_path.string()}};
  environment.digest_ = environment_digest(environment.profile_id_, environment.entries_);
  return environment;
}

ava::core::VoidResult validate_environment_matches_plan(CommandEnvironment const& environment, CommandPlan const& plan)
{
  if (environment.profile_id() != plan.environment_profile_id() || environment.digest() != plan.environment_digest() ||
      environment.roots_ != plan.synthetic_environment_roots_)
  {
    return std::unexpected(
        command_error(ava::core::ErrorCategory::PermissionDenied, "prepared command environment does not match the sealed plan environment digest"));
  }
  return {};
}

}  // namespace ava::command::detail

namespace ava::command {

CommandEnvironment::CommandEnvironment(FactoryPasskey, SyntheticEnvironmentRoots roots) : roots_(std::move(roots))
{
}

std::string const& CommandEnvironment::profile_id() const noexcept
{
  return profile_id_;
}

std::string const& CommandEnvironment::digest() const noexcept
{
  return digest_;
}

std::vector<EnvironmentVariable> const& CommandEnvironment::entries() const noexcept
{
  return entries_;
}

}  // namespace ava::command
