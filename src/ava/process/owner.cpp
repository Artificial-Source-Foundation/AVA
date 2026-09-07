#include "sys.h"
#include "ava/process/owner.h"
#include "ava/process/types.h"
#include "ava/core/error.h"
#include "ava/core/ids.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ava::process {
namespace {

ava::core::Error owner_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

bool valid_generated_segment(std::string const& value) noexcept
{
  if (value.empty() || value.size() > kMaxOwnerSegmentBytesV1)
    return false;
  for (unsigned char character : value)
  {
    if (!std::isalnum(character) && character != '_' && character != '-')
      return false;
  }
  return true;
}

ava::core::Result<std::string> generate_segment()
{
  try
  {
    auto value = ava::core::make_id("process_owner");
    if (!valid_generated_segment(value))
      return std::unexpected(owner_error("generated process owner segment is outside the version-1 bounds"));
    return value;
  }
  catch (std::exception const& error)
  {
    return std::unexpected(owner_error("failed to generate an opaque process owner segment").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(owner_error("failed to generate an opaque process owner segment"));
  }
}

}  // namespace

OwnerPathV1::OwnerPathV1(std::array<std::optional<std::string>, 4> segments) noexcept : segments_(std::move(segments))
{
}

ava::core::Result<OwnerPathV1> OwnerPathV1::application()
{
  auto generated = generate_segment();
  if (!generated)
    return std::unexpected(std::move(generated.error()));
  std::array<std::optional<std::string>, 4> segments;
  segments[static_cast<std::size_t>(Segment::Application)] = std::move(*generated);
  return OwnerPathV1(std::move(segments));
}

ava::core::Result<OwnerPathV1> OwnerPathV1::session() const
{
  return append(Segment::Session);
}

ava::core::Result<OwnerPathV1> OwnerPathV1::run() const
{
  return append(Segment::Run);
}

ava::core::Result<OwnerPathV1> OwnerPathV1::operation() const
{
  return append(Segment::Operation);
}

ava::core::Result<OwnerPathV1> OwnerPathV1::append(Segment segment) const
{
  if (!is_valid_prefix())
    return std::unexpected(owner_error("cannot derive a process owner from an invalid owner prefix"));

  auto const session_index = static_cast<std::size_t>(Segment::Session);
  auto const run_index = static_cast<std::size_t>(Segment::Run);
  auto const operation_index = static_cast<std::size_t>(Segment::Operation);
  if (segments_[operation_index])
    return std::unexpected(owner_error("cannot derive a process owner below an operation"));
  if (segment == Segment::Session && (segments_[session_index] || segments_[run_index]))
    return std::unexpected(owner_error("session owner segment already exists"));
  if (segment == Segment::Run && (!segments_[session_index] || segments_[run_index]))
    return std::unexpected(owner_error("run owner requires exactly one generated session parent"));
  if (segment == Segment::Operation && segments_[operation_index])
    return std::unexpected(owner_error("operation owner segment already exists"));

  auto generated = generate_segment();
  if (!generated)
    return std::unexpected(std::move(generated.error()));
  auto child = segments_;
  child[static_cast<std::size_t>(segment)] = std::move(*generated);
  return OwnerPathV1(std::move(child));
}

bool OwnerPathV1::is_valid_prefix() const noexcept
{
  if (schema_version_ != kProcessSchemaVersionV1 || !segments_[static_cast<std::size_t>(Segment::Application)])
    return false;
  if (segments_[static_cast<std::size_t>(Segment::Run)] && !segments_[static_cast<std::size_t>(Segment::Session)])
    return false;
  for (auto const& segment : segments_)
  {
    if (segment && !valid_generated_segment(*segment))
      return false;
  }
  return true;
}

bool OwnerPathV1::is_launch_owner() const noexcept
{
  return is_valid_prefix() && segments_[static_cast<std::size_t>(Segment::Operation)].has_value();
}

bool OwnerPathV1::matches_prefix(OwnerPathV1 const& prefix) const noexcept
{
  if (!is_valid_prefix() || !prefix.is_valid_prefix())
    return false;
  for (std::size_t index = 0; index < segments_.size(); ++index)
  {
    if (prefix.segments_[index] && segments_[index] != prefix.segments_[index])
      return false;
  }
  return true;
}

std::size_t OwnerPathV1::depth() const noexcept
{
  std::size_t result = 0;
  for (auto const& segment : segments_)
    result += segment.has_value() ? 1U : 0U;
  return result;
}

std::size_t OwnerPathV1::encoded_size() const noexcept
{
  constexpr std::array<std::string_view, 4> labels{"application/", "/session/", "/run/", "/operation/"};
  std::size_t result = 0;
  for (std::size_t index = 0; index < segments_.size(); ++index)
  {
    if (segments_[index])
      result += labels[index].size() + segments_[index]->size();
  }
  return result;
}

std::uint32_t OwnerPathV1::schema_version() const noexcept
{
  return schema_version_;
}

std::string OwnerPathV1::key() const
{
  std::string result;
  result.reserve(encoded_size());
  constexpr std::array<std::string_view, 4> labels{"application/", "/session/", "/run/", "/operation/"};
  for (std::size_t index = 0; index < segments_.size(); ++index)
  {
    if (segments_[index])
    {
      result += labels[index];
      result += *segments_[index];
    }
  }
  return result;
}

}  // namespace ava::process
