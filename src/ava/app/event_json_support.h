#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::detail {

void append_event_string_field(std::string& out, std::string_view key, std::string_view value);
void append_event_number_field(std::string& out, std::string_view key, std::size_t value);
void append_event_bool_field(std::string& out, std::string_view key, bool value);
void append_event_required_string_field(std::string& out, std::string_view key, std::string_view value);
void append_event_optional_string_field(std::string& out, std::string_view key,
                                        std::optional<std::string> const& value);
void append_event_string_array_field(std::string& out, std::string_view key, std::vector<std::string> const& values);
void append_event_json_object_field(std::string& out, std::string_view key, std::string_view value);

void append_payload_string_field(std::string& out, bool& has_field, std::string_view key, std::string_view value);
void append_payload_number_field(std::string& out, bool& has_field, std::string_view key, std::size_t value);
void append_payload_bool_field(std::string& out, bool& has_field, std::string_view key, bool value);
void append_payload_json_object_field(std::string& out, bool& has_field, std::string_view key, std::string_view value);
void append_payload_string_array_field(std::string& out, bool& has_field, std::string_view key,
                                       std::vector<std::string> const& values);

}  // namespace ava::app::detail
