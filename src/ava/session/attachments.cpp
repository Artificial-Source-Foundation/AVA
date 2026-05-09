#include "ava/session/attachments.h"

#include "ava/session/session_store.h"

#include "ava/core/error.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ava::session {
namespace {

constexpr std::size_t kMaxImageBytes = 20 * 1024 * 1024;

bool has_control_byte(std::string_view value)
{
  for (auto const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) return true;
  }
  return false;
}

bool hex_string(std::string_view value)
{
  for (auto const ch : value) {
    if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) return false;
  }
  return true;
}

bool valid_attachment_storage_path(std::string_view path)
{
  if (path.empty() || path.size() > 4096 || has_control_byte(path)) return false;
  if (!path.starts_with("attachments/")) return false;
  if (path.starts_with('/') || path.starts_with('~') || path.find('\\') != std::string_view::npos) return false;
  if (path.find(':') != std::string_view::npos) return false;
  std::size_t segment_start = 0;
  while (segment_start <= path.size()) {
    auto const slash = path.find('/', segment_start);
    auto const segment = path.substr(segment_start, slash == std::string_view::npos ? std::string_view::npos : slash - segment_start);
    if (segment.empty() || segment == "." || segment == "..") return false;
    if (slash == std::string_view::npos) break;
    segment_start = slash + 1;
  }
  return true;
}

ava::core::Error attachment_error(ava::core::ErrorCategory category, std::string_view message,
                                  std::string_view storage_path)
{
  auto error = ava::core::Error(category, std::string(message));
  error.with_context("storage_path", std::string(storage_path));
  return error;
}

std::uint32_t rotr(std::uint32_t value, std::uint32_t count)
{
  return (value >> count) | (value << (32U - count));
}

std::array<std::uint8_t, 32> sha256(std::string_view bytes)
{
  static constexpr std::array<std::uint32_t, 64> k{
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

  std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
  auto const bit_len = static_cast<std::uint64_t>(data.size()) * 8ULL;
  data.push_back(0x80U);
  while ((data.size() % 64U) != 56U) data.push_back(0U);
  for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>((bit_len >> shift) & 0xFFU));

  std::array<std::uint32_t, 8> hash{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  for (std::size_t chunk = 0; chunk < data.size(); chunk += 64U) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16U; ++i) {
      auto const offset = chunk + i * 4U;
      w[i] = (static_cast<std::uint32_t>(data[offset]) << 24U) |
             (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
             (static_cast<std::uint32_t>(data[offset + 2]) << 8U) | static_cast<std::uint32_t>(data[offset + 3]);
    }
    for (std::size_t i = 16U; i < 64U; ++i) {
      auto const s0 = rotr(w[i - 15U], 7U) ^ rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
      auto const s1 = rotr(w[i - 2U], 17U) ^ rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
      w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }
    auto a = hash[0];
    auto b = hash[1];
    auto c = hash[2];
    auto d = hash[3];
    auto e = hash[4];
    auto f = hash[5];
    auto g = hash[6];
    auto h = hash[7];
    for (std::size_t i = 0; i < 64U; ++i) {
      auto const s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
      auto const ch = (e & f) ^ ((~e) & g);
      auto const temp1 = h + s1 + ch + k[i] + w[i];
      auto const s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
      auto const maj = (a & b) ^ (a & c) ^ (b & c);
      auto const temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
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

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t index = 0; index < hash.size(); ++index) {
    digest[index * 4U] = static_cast<std::uint8_t>((hash[index] >> 24U) & 0xFFU);
    digest[index * 4U + 1U] = static_cast<std::uint8_t>((hash[index] >> 16U) & 0xFFU);
    digest[index * 4U + 2U] = static_cast<std::uint8_t>((hash[index] >> 8U) & 0xFFU);
    digest[index * 4U + 3U] = static_cast<std::uint8_t>(hash[index] & 0xFFU);
  }
  return digest;
}

std::string sha256_hex(std::string_view bytes)
{
  auto digest = sha256(bytes);
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (auto const byte : digest) out << std::setw(2) << static_cast<int>(byte);
  return out.str();
}

}  // namespace

std::filesystem::path attachment_storage_root(SessionStore const& store)
{
  return store.session_path().parent_path() / (store.session_id() + ".attachments");
}

ava::core::Result<std::filesystem::path> resolve_attachment_storage_path(SessionStore const& store,
                                                                           std::string_view storage_path)
{
  if (!valid_attachment_storage_path(storage_path)) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                            "image attachment storage path is invalid", storage_path));
  }
  auto const root = attachment_storage_root(store).lexically_normal();
  auto const resolved = (root / std::filesystem::path(storage_path)).lexically_normal();
  auto const root_string = root.string();
  auto const resolved_string = resolved.string();
  if (resolved_string != root_string && resolved_string.rfind(root_string + std::string(1, std::filesystem::path::preferred_separator), 0) != 0) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::PermissionDenied,
                                            "image attachment path escapes session attachment storage", storage_path));
  }
  return resolved;
}

ava::core::VoidResult validate_attachment_path_components(SessionStore const& store, std::string_view storage_path)
{
  auto current = attachment_storage_root(store);
  std::error_code status_error;
  auto status = std::filesystem::symlink_status(current, status_error);
  if (status_error) {
    auto error = attachment_error(ava::core::ErrorCategory::Io, "failed to inspect image attachment storage",
                                  storage_path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status)) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::PermissionDenied,
                                            "image attachment storage must not be a symlink", storage_path));
  }
  auto const relative = std::filesystem::path(storage_path);
  auto component = relative.begin();
  while (component != relative.end()) {
    auto next = component;
    ++next;
    if (next == relative.end()) break;
    current /= *component;
    status_error.clear();
    status = std::filesystem::symlink_status(current, status_error);
    if (status_error) {
      auto error = attachment_error(ava::core::ErrorCategory::Io, "failed to inspect image attachment directory",
                                    storage_path);
      error.with_context("cause", status_error.message());
      return std::unexpected(std::move(error));
    }
    if (std::filesystem::is_symlink(status)) {
      return std::unexpected(attachment_error(ava::core::ErrorCategory::PermissionDenied,
                                              "image attachment directory must not be a symlink", storage_path));
    }
    if (!std::filesystem::is_directory(status)) {
      return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                              "image attachment parent is not a directory", storage_path));
    }
    component = next;
  }
  return {};
}

ava::core::Result<LoadedImageAttachment> load_image_attachment(SessionStore const& store,
                                                               ImageAttachmentRef const& attachment)
{
  if (attachment.byte_size == 0 || attachment.byte_size > kMaxImageBytes) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                            "image attachment byte size is invalid", attachment.storage_path));
  }
  if (attachment.sha256.size() != 64 || !hex_string(attachment.sha256)) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                            "image attachment sha256 is invalid", attachment.storage_path));
  }
  auto path = resolve_attachment_storage_path(store, attachment.storage_path);
  if (!path) return std::unexpected(std::move(path.error()));
  if (auto valid_components = validate_attachment_path_components(store, attachment.storage_path); !valid_components) {
    return std::unexpected(std::move(valid_components.error()));
  }

  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(*path, status_error);
  if (status_error) {
    auto error = attachment_error(ava::core::ErrorCategory::Io, "failed to inspect image attachment", attachment.storage_path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status)) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::PermissionDenied,
                                            "image attachment path must not be a symlink", attachment.storage_path));
  }
  if (!std::filesystem::is_regular_file(status)) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                            "image attachment path is not a regular file", attachment.storage_path));
  }
  auto const actual_size = std::filesystem::file_size(*path, status_error);
  if (status_error) {
    auto error = attachment_error(ava::core::ErrorCategory::Io, "failed to stat image attachment", attachment.storage_path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (actual_size != attachment.byte_size) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                            "image attachment byte size does not match metadata", attachment.storage_path));
  }

  std::ifstream file(*path, std::ios::binary);
  if (!file) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::Io, "failed to open image attachment",
                                            attachment.storage_path));
  }
  std::string bytes(static_cast<std::size_t>(actual_size), '\0');
  if (!bytes.empty()) file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!file || file.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::Io, "failed to read image attachment",
                                            attachment.storage_path));
  }
  if (sha256_hex(bytes) != attachment.sha256) {
    return std::unexpected(attachment_error(ava::core::ErrorCategory::InvalidArgument,
                                            "image attachment sha256 does not match metadata", attachment.storage_path));
  }
  return LoadedImageAttachment{.metadata = attachment, .bytes = std::move(bytes), .path = *path};
}

}  // namespace ava::session
