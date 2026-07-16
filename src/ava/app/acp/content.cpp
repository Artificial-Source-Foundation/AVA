#include "sys.h"
#include "ava/app/acp/content.h"
#include "ava/provider/provider.h"
#include "ava/provider/provider_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

namespace ava::app::acp {
namespace {

using Json = nlohmann::json;

JsonRpcError content_error(std::string message)
{
  return JsonRpcError{.code = -32602,
                      .message = std::move(message),
                      .data_json = std::nullopt,
                      .id = std::nullopt,
                      .intent = EnvelopeIntent::Unknown,
                      .suppress_response = false};
}

bool has_control_byte(std::string_view value)
{
  return std::ranges::any_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte < 0x20 || byte == 0x7f;
  });
}

std::optional<std::string> decode_base64_canonical(std::string_view encoded)
{
  if (encoded.empty() || encoded.size() % 4 != 0 || !ava::provider::is_valid_base64(encoded))
    return std::nullopt;
  if (encoded.size() > ((kMaxAcpPromptImageBytes + 2U) / 3U) * 4U)
    return std::nullopt;

  auto value = [](char ch) -> std::optional<std::uint8_t> {
    if (ch >= 'A' && ch <= 'Z')
      return static_cast<std::uint8_t>(ch - 'A');
    if (ch >= 'a' && ch <= 'z')
      return static_cast<std::uint8_t>(ch - 'a' + 26);
    if (ch >= '0' && ch <= '9')
      return static_cast<std::uint8_t>(ch - '0' + 52);
    if (ch == '+')
      return 62;
    if (ch == '/')
      return 63;
    return std::nullopt;
  };

  std::string decoded;
  decoded.reserve(encoded.size() / 4 * 3);
  for (std::size_t offset = 0; offset < encoded.size(); offset += 4)
  {
    bool const final = offset + 4 == encoded.size();
    auto a = value(encoded[offset]);
    auto b = value(encoded[offset + 1]);
    bool const c_padding = encoded[offset + 2] == '=';
    bool const d_padding = encoded[offset + 3] == '=';
    auto c = c_padding ? std::optional<std::uint8_t>(0) : value(encoded[offset + 2]);
    auto d = d_padding ? std::optional<std::uint8_t>(0) : value(encoded[offset + 3]);
    if (!a || !b || !c || !d)
      return std::nullopt;
    if (!final && (c_padding || d_padding))
      return std::nullopt;
    if (c_padding && !d_padding)
      return std::nullopt;
    if (c_padding && (*b & 0x0fU) != 0)
      return std::nullopt;
    if (d_padding && !c_padding && (*c & 0x03U) != 0)
      return std::nullopt;

    decoded.push_back(static_cast<char>((*a << 2U) | (*b >> 4U)));
    if (!c_padding)
    {
      decoded.push_back(static_cast<char>((*b << 4U) | (*c >> 2U)));
      if (!d_padding)
        decoded.push_back(static_cast<char>((*c << 6U) | *d));
    }
    if (decoded.size() > kMaxAcpPromptImageBytes)
      return std::nullopt;
  }
  if (decoded.empty() || ava::provider::base64_encode(decoded) != encoded)
    return std::nullopt;
  return decoded;
}

void append_prompt_line(std::string& text, std::string_view line)
{
  if (!text.empty())
    text.push_back('\n');
  text.append(line);
}

}  // namespace

PromptContentDecodeResult decode_prompt_content(std::string_view prompt_json)
{
  auto prompt = Json::parse(prompt_json, nullptr, false, true);
  if (!prompt.is_array() || prompt.empty() || prompt.size() > kMaxAcpPromptBlocks)
    return std::unexpected(content_error("session/prompt requires a non-empty bounded prompt array"));

  AcpPromptContent content;
  std::size_t image_bytes = 0;
  for (auto const& block : prompt)
  {
    if (!block.is_object())
      return std::unexpected(content_error("prompt content blocks must be objects"));
    auto type = block.find("type");
    if (type == block.end() || !type->is_string() || type->get_ref<std::string const&>().empty())
      return std::unexpected(content_error("prompt content block type is required"));
    auto const& discriminator = type->get_ref<std::string const&>();
    if (discriminator == "text")
    {
      auto text = block.find("text");
      if (text == block.end() || !text->is_string())
        return std::unexpected(content_error("text prompt blocks require text"));
      if (content.text.size() + text->get_ref<std::string const&>().size() + (content.text.empty() ? 0U : 1U) > kMaxAcpPromptTextBytes)
        return std::unexpected(content_error("prompt text exceeds the ACP prompt byte limit"));
      append_prompt_line(content.text, text->get_ref<std::string const&>());
      continue;
    }

    if (discriminator == "resource_link")
    {
      auto name = block.find("name");
      auto uri = block.find("uri");
      if (name == block.end() || uri == block.end() || !name->is_string() || !uri->is_string() || name->get_ref<std::string const&>().empty() ||
          uri->get_ref<std::string const&>().empty() || name->get_ref<std::string const&>().size() > 4096 ||
          uri->get_ref<std::string const&>().size() > 16U * 1024U || has_control_byte(name->get_ref<std::string const&>()) ||
          has_control_byte(uri->get_ref<std::string const&>()))
      {
        return std::unexpected(content_error("resource_link prompt blocks require bounded safe name and uri strings"));
      }
      std::string const reference = "Resource link reference (not fetched): " + name->get<std::string>() + " <" + uri->get<std::string>() + ">";
      if (content.text.size() + reference.size() + (content.text.empty() ? 0U : 1U) > kMaxAcpPromptTextBytes)
        return std::unexpected(content_error("prompt text exceeds the ACP prompt byte limit"));
      append_prompt_line(content.text, reference);
      continue;
    }

    if (discriminator == "image")
    {
      if (content.images.size() >= kMaxAcpPromptImages)
        return std::unexpected(content_error("prompt image count exceeds the ACP limit"));
      auto data = block.find("data");
      auto mime = block.find("mimeType");
      if (data == block.end() || mime == block.end() || !data->is_string() || !mime->is_string() ||
          !ava::provider::is_supported_image_mime_type(mime->get_ref<std::string const&>()) || has_control_byte(mime->get_ref<std::string const&>()))
        return std::unexpected(content_error("image prompt blocks require an exactly supported MIME type and base64 data"));
      auto decoded = decode_base64_canonical(data->get_ref<std::string const&>());
      if (!decoded)
        return std::unexpected(content_error("image data must be canonical base64 within the ACP image byte limit"));
      image_bytes += decoded->size();
      if (image_bytes > kMaxAcpPromptImageTotalBytes)
        return std::unexpected(content_error("prompt images exceed the ACP aggregate image byte limit"));
      content.images.push_back(AcpPromptImage{.mime_type = mime->get<std::string>(), .bytes = std::move(*decoded)});
      continue;
    }

    if (discriminator == "audio" || discriminator == "resource")
      return std::unexpected(content_error("audio and embedded resource prompt content are not supported or advertised by AVA ACP"));
    return std::unexpected(content_error("prompt content discriminator is not supported or advertised by AVA ACP"));
  }

  if (content.text.empty() && content.images.empty())
    return std::unexpected(content_error("session/prompt requires text, image, or resource_link content"));
  if (content.text.empty())
    content.text = "[Image attachment]";
  return content;
}

}  // namespace ava::app::acp
