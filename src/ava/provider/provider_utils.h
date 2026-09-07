#pragma once

#include "ava/core/result.h"

#include <string>
#include <string_view>

namespace ava::provider {

struct ProviderRequest;

[[nodiscard]] bool is_json_object_shape(std::string_view value);
[[nodiscard]] bool is_valid_json_object(std::string_view value);
[[nodiscard]] std::string base64_encode(std::string_view bytes);
[[nodiscard]] bool is_valid_base64(std::string_view value);

// Strict outbound image validation for request serialization, shared by the
// OpenAI Responses and OpenAI-compatible chat serializers: image parts must
// sit in a user message, carry a supported MIME type, and hold nonempty valid
// base64 bytes, checked in that order. provider_name prefixes each error.
[[nodiscard]] ava::core::VoidResult validate_outbound_image_payloads(ProviderRequest const& request, std::string_view provider_name);

}  // namespace ava::provider
