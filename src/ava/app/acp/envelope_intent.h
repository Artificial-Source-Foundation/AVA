#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/acp/protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ava::app::acp {

struct EnvelopeScanResult
{
  EnvelopeIntent intent = EnvelopeIntent::Unknown;
  bool top_level_object = false;
  bool ambiguous = false;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Incrementally identifies only the top-level JSON-RPC envelope keys. The
// scanner uses fixed state, never recurses, and never retains member values.
// It is therefore safe to keep feeding after the transport stops buffering an
// oversized record.
class EnvelopeIntentScanner
{
 public:
  void consume(char ch) noexcept;
  [[nodiscard]] EnvelopeScanResult finish() noexcept;

 private:
  enum class State : std::uint8_t
  {
    Leading,
    KeyOrEnd,
    Key,
    Colon,
    Value,
    Primitive,
    CommaOrEnd,
    Complete,
    Invalid,
  };

  enum class StringRole : std::uint8_t
  {
    None,
    Key,
    Value,
    Nested,
  };

  enum class Primitive : std::uint8_t
  {
    None,
    True,
    False,
    Null,
    Number,
  };

  enum class NumberState : std::uint8_t
  {
    Minus,
    Zero,
    Integer,
    Dot,
    Fraction,
    Exponent,
    ExponentSign,
    ExponentDigits,
  };

  void consume_string(char ch) noexcept;
  void begin_string(StringRole role) noexcept;
  void finish_string() noexcept;
  void consume_key_character(unsigned value) noexcept;
  void begin_primitive(char ch) noexcept;
  void consume_primitive(char ch) noexcept;
  [[nodiscard]] bool primitive_complete() const noexcept;
  void open_container(char ch) noexcept;
  [[nodiscard]] bool close_container(char ch) noexcept;
  void mark_malformed() noexcept;

  static constexpr std::size_t kTrackedNesting = 256;

  State state_ = State::Leading;
  StringRole string_role_ = StringRole::None;
  Primitive primitive_ = Primitive::None;
  NumberState number_state_ = NumberState::Minus;
  std::array<char, kTrackedNesting> containers_{};
  std::size_t depth_ = 0;
  std::size_t key_length_ = 0;
  std::size_t literal_offset_ = 0;
  unsigned unicode_value_ = 0;
  unsigned unicode_digits_remaining_ = 0;
  std::uint8_t key_candidates_ = 0;
  bool in_string_ = false;
  bool escaped_ = false;
  bool root_object_ = false;
  bool malformed_ = false;
  bool untracked_nesting_ = false;
  bool saw_id_ = false;
  bool saw_method_ = false;
  bool saw_reliable_method_ = false;
  bool duplicate_envelope_key_ = false;
  bool key_is_id_ = false;
  bool key_is_method_ = false;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] EnvelopeScanResult scan_envelope_intent(std::string_view record) noexcept;

// Oversized malformed, duplicate, or deeper-than-tracked objects may have an
// ambiguous envelope. If no method was safely established, suppression is the
// loop-safe choice because the record may have been a response whose id could
// not be classified.
[[nodiscard]] EnvelopeIntent loop_safe_oversized_intent(EnvelopeScanResult const& scan) noexcept;

}  // namespace ava::app::acp
