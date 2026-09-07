#include "sys.h"
#include "ava/provider/provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"
#include "ava/core/string_utils.h"

#include <cctype>

namespace ava::provider {

bool is_json_object_shape(std::string_view value)
{
  return ava::core::json::is_valid_object(value);
}

bool is_valid_json_object(std::string_view value)
{
  return ava::core::json::is_valid_object(value);
}

std::string base64_encode(std::string_view bytes)
{
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < bytes.size(); index += 3)
  {
    unsigned int value = static_cast<unsigned char>(bytes[index]) << 16;
    bool const has_second = index + 1 < bytes.size();
    bool const has_third = index + 2 < bytes.size();
    if (has_second)
      value |= static_cast<unsigned char>(bytes[index + 1]) << 8;
    if (has_third)
      value |= static_cast<unsigned char>(bytes[index + 2]);
    encoded.push_back(kAlphabet[(value >> 18) & 0x3f]);
    encoded.push_back(kAlphabet[(value >> 12) & 0x3f]);
    encoded.push_back(has_second ? kAlphabet[(value >> 6) & 0x3f] : '=');
    encoded.push_back(has_third ? kAlphabet[value & 0x3f] : '=');
  }
  return encoded;
}

ava::core::VoidResult validate_outbound_image_payloads(ProviderRequest const& request, std::string_view provider_name)
{
  for (std::size_t message_index = 0; message_index < request.messages.size(); ++message_index)
  {
    auto const& message = request.messages[message_index];
    for (std::size_t part_index = 0; part_index < message.content_parts.size(); ++part_index)
    {
      auto const& part = message.content_parts[part_index];
      if (part.type != ContentPartType::Image)
        continue;
      if (message.role != "user")
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(provider_name) + " image content requires user role");
        error.with_context("message_index", std::to_string(message_index));
        error.with_context("content_part_index", std::to_string(part_index));
        return std::unexpected(std::move(error));
      }
      if (!is_supported_image_mime_type(part.mime_type))
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(provider_name) + " image MIME type is not supported");
        error.with_context("message_index", std::to_string(message_index));
        error.with_context("content_part_index", std::to_string(part_index));
        return std::unexpected(std::move(error));
      }
      if (part.data_base64.empty() || !is_valid_base64(part.data_base64))
      {
        auto error =
            ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(provider_name) + " image content requires verified attachment bytes");
        error.with_context("message_index", std::to_string(message_index));
        error.with_context("content_part_index", std::to_string(part_index));
        return std::unexpected(std::move(error));
      }
    }
  }
  return {};
}

bool is_valid_base64(std::string_view value)
{
  if (value.empty() || value.size() % 4 != 0)
    return false;
  bool padding = false;
  std::size_t padding_count = 0;
  for (char const ch : value)
  {
    bool const is_alphabet = std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '/';
    if (ch == '=')
    {
      padding = true;
      ++padding_count;
      if (padding_count > 2)
        return false;
      continue;
    }
    if (!is_alphabet || padding)
      return false;
  }
  return padding_count == 0 || value.ends_with(std::string(padding_count, '='));
}

}  // namespace ava::provider
