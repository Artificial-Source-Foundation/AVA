#include "ava/config/oauth_crypto_support.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <bit>
#include <cerrno>
#include <cstring>
#include <utility>

namespace ava::config::detail {
namespace {

constexpr std::string_view kBase64UrlAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

class ScopedFd {
 public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other) {
      close_if_open();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd() { close_if_open(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close_if_open() noexcept
  {
    if (fd_ >= 0) static_cast<void>(::close(fd_));
  }

  int fd_ = -1;
};

std::string errno_message()
{
  return std::strerror(errno);
}

std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
  return (x & y) ^ (~x & z);
}

std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
  return (x & y) ^ (x & z) ^ (y & z);
}

std::uint32_t big_sigma0(std::uint32_t x)
{
  return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

std::uint32_t big_sigma1(std::uint32_t x)
{
  return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

std::uint32_t small_sigma0(std::uint32_t x)
{
  return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
}

std::uint32_t small_sigma1(std::uint32_t x)
{
  return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10U);
}

std::array<std::uint8_t, 32> sha256(std::string_view text)
{
  constexpr std::array<std::uint32_t, 64> kConstants{
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

  std::vector<std::uint8_t> data(text.begin(), text.end());
  auto const bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
  data.push_back(0x80U);
  while ((data.size() % 64) != 56) data.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8) {
    data.push_back(static_cast<std::uint8_t>((bit_length >> static_cast<unsigned>(shift)) & 0xFFU));
  }

  std::array<std::uint32_t, 8> hash{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

  for (std::size_t chunk = 0; chunk < data.size(); chunk += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      auto const offset = chunk + index * 4;
      words[index] =
          (static_cast<std::uint32_t>(data[offset]) << 24U) | (static_cast<std::uint32_t>(data[offset + 1]) << 16U) |
          (static_cast<std::uint32_t>(data[offset + 2]) << 8U) | static_cast<std::uint32_t>(data[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      words[index] =
          small_sigma1(words[index - 2]) + words[index - 7] + small_sigma0(words[index - 15]) + words[index - 16];
    }

    auto a = hash[0];
    auto b = hash[1];
    auto c = hash[2];
    auto d = hash[3];
    auto e = hash[4];
    auto f = hash[5];
    auto g = hash[6];
    auto h = hash[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      auto const temp1 = h + big_sigma1(e) + choose(e, f, g) + kConstants[index] + words[index];
      auto const temp2 = big_sigma0(a) + majority(a, b, c);
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
    digest[index * 4] = static_cast<std::uint8_t>((hash[index] >> 24U) & 0xFFU);
    digest[index * 4 + 1] = static_cast<std::uint8_t>((hash[index] >> 16U) & 0xFFU);
    digest[index * 4 + 2] = static_cast<std::uint8_t>((hash[index] >> 8U) & 0xFFU);
    digest[index * 4 + 3] = static_cast<std::uint8_t>(hash[index] & 0xFFU);
  }
  return digest;
}

}  // namespace

std::string base64_url_encode(std::span<std::uint8_t const> bytes)
{
  std::string output;
  output.reserve(((bytes.size() + 2) / 3) * 4);
  std::size_t index = 0;
  while (index + 3 <= bytes.size()) {
    auto const block = (static_cast<std::uint32_t>(bytes[index]) << 16U) |
                       (static_cast<std::uint32_t>(bytes[index + 1]) << 8U) |
                       static_cast<std::uint32_t>(bytes[index + 2]);
    output.push_back(kBase64UrlAlphabet[(block >> 18U) & 0x3FU]);
    output.push_back(kBase64UrlAlphabet[(block >> 12U) & 0x3FU]);
    output.push_back(kBase64UrlAlphabet[(block >> 6U) & 0x3FU]);
    output.push_back(kBase64UrlAlphabet[block & 0x3FU]);
    index += 3;
  }
  auto const remaining = bytes.size() - index;
  if (remaining == 1) {
    auto const block = static_cast<std::uint32_t>(bytes[index]) << 16U;
    output.push_back(kBase64UrlAlphabet[(block >> 18U) & 0x3FU]);
    output.push_back(kBase64UrlAlphabet[(block >> 12U) & 0x3FU]);
  } else if (remaining == 2) {
    auto const block =
        (static_cast<std::uint32_t>(bytes[index]) << 16U) | (static_cast<std::uint32_t>(bytes[index + 1]) << 8U);
    output.push_back(kBase64UrlAlphabet[(block >> 18U) & 0x3FU]);
    output.push_back(kBase64UrlAlphabet[(block >> 12U) & 0x3FU]);
    output.push_back(kBase64UrlAlphabet[(block >> 6U) & 0x3FU]);
  }
  return output;
}

std::optional<std::vector<std::uint8_t>> base64_url_decode(std::string_view value)
{
  std::vector<std::uint8_t> output;
  output.reserve((value.size() * 3) / 4);
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char const ch : value) {
    if (ch == '=') break;
    auto const position = kBase64UrlAlphabet.find(ch);
    if (position == std::string_view::npos) return std::nullopt;
    buffer = (buffer << 6U) | static_cast<std::uint32_t>(position);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<std::uint8_t>((buffer >> static_cast<unsigned>(bits)) & 0xFFU));
    }
  }
  return output;
}

std::string code_challenge(std::string_view verifier)
{
  return base64_url_encode(sha256(verifier));
}

std::string url_encode(std::string_view value)
{
  constexpr std::string_view hex = "0123456789ABCDEF";
  std::string output;
  output.reserve(value.size());
  for (unsigned char const byte : value) {
    if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
        byte == '_' || byte == '.' || byte == '~') {
      output.push_back(static_cast<char>(byte));
    } else {
      output.push_back('%');
      output.push_back(hex[(byte >> 4U) & 0x0FU]);
      output.push_back(hex[byte & 0x0FU]);
    }
  }
  return output;
}

ava::core::Result<std::string> random_token(std::size_t bytes)
{
  std::vector<std::uint8_t> data(bytes);
  ScopedFd const fd(::open("/dev/urandom", O_RDONLY | O_CLOEXEC));
  if (fd.get() < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open randomness source");
    error.with_context("path", "/dev/urandom");
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  std::size_t offset = 0;
  while (offset < data.size()) {
    auto const count = ::read(fd.get(), data.data() + offset, data.size() - offset);
    if (count < 0) {
      if (errno == EINTR) continue;
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read randomness source");
      error.with_context("path", "/dev/urandom");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (count == 0) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "randomness source returned no data"));
    }
    offset += static_cast<std::size_t>(count);
  }
  return base64_url_encode(data);
}

}  // namespace ava::config::detail
