#pragma once

#include "utils/Badge.h"
#include "ava/debug/print_members_on.h"
#include "ava/app/runtime/Session.h"
#include "ava/core/result.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ava::app::rpc::detail {

// Serialize the v1-compatible "messages" RPC result for a runtime session.
//
// Constructed exclusively by runtime::Session::messages_result_json, the only
// caller able to mint a utils::Badge<runtime::Session>; the badge parameter
// makes the construction privilege compiler-enforced rather than convention.
// Every session-entry RPC helper used while running stays anonymous-namespace
// private to serialization.cpp, so this class is the single bridge between the
// Session member and that RPC implementation.
//
// The serializer is single-use. The byte/entry budgets and the legacy-only
// fallback are identical to the historical free-function implementation; the
// method-object split only exposes the pipeline phases and gives the
// intermediate candidate/render buffers a stable home.
class MessagesResultSerializer
{
 public:
  // The session is retained by reference for the lifetime of the serializer.
  // Called from Session::messages_result_json that passes *this: session is already locked.
  MessagesResultSerializer(utils::Badge<runtime::Session>, runtime::Session const& session);

  MessagesResultSerializer(MessagesResultSerializer const&) = delete;
  MessagesResultSerializer& operator=(MessagesResultSerializer const&) = delete;

  // Drive load -> project -> select -> refill -> assemble and return the
  // assembled JSON, or propagate a load/projection error from the session store.
  // The rvalue qualifier marks the serializer as spent after a single run.
  [[nodiscard]] ava::core::Result<std::string> run() &&;

 private:
  // One retained projected session entry plus its pre-rendered legacy form.
  // The legacy representation deliberately omits ordered_output; that detail is
  // layered back in by refill_ordered_output only when the per-entry byte
  // budget still allows it, so ordered detail can never evict legacy payload.
  struct MessageCandidate
  {
    ava::session::SessionEntry const* entry = nullptr;
    std::string legacy_json;
    bool has_ordered_output = false;

    // References live session content that may carry provider credentials.
    AVA_DEBUG_PRINT_MEMBERS_ON
  };

  // Pipeline phases. load_and_project is the only fallible step.
  [[nodiscard]] ava::core::VoidResult load_and_project();
  void collect_candidates();
  void select_legacy_messages();
  void refill_ordered_output();
  [[nodiscard]] std::string assemble_json() const;

  // Hypothetical response size and tail given the current rendered_ buffer and
  // the passed truncation/omission flags. Used to test-fit candidates against
  // the byte budget without committing their state to the member fields.
  [[nodiscard]] std::size_t rendered_size_for(bool truncated, std::size_t ordered_output_omitted_count) const;
  [[nodiscard]] std::string tail_json_for(bool truncated, std::size_t message_count, std::size_t ordered_output_omitted_count) const;

  runtime::Session const& session_;
  std::string prefix_;
  std::vector<ava::session::SessionEntry> projected_;
  std::vector<MessageCandidate> candidates_;
  std::vector<MessageCandidate const*> selected_;
  std::vector<std::string> rendered_;
  bool truncated_ = false;
  std::size_t ordered_output_omitted_count_ = 0;

  // References live session content that may carry provider credentials.
  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Serialize the read-only session query results served by the session RPC
// commands (list_sessions, session_tree, list_models, session_stats,
// session_validation).
//
// Like MessagesResultSerializer, construction is gated by
// utils::Badge<runtime::Session> so the runtime::Session member functions are
// the only callers, and every RPC helper used while running stays anonymous-
// namespace private to serialization.cpp. The five methods are independent
// and stateless beyond the retained session reference, so a single instance
// can drive any subset of them; one method does not observe state produced by
// another.
class SessionResultSerializer
{
 public:
  // The session is retained by reference for the lifetime of the serializer.
  // Called from Session::*_json[_1] member functions that pass *this: session is already locked.
  SessionResultSerializer(utils::Badge<runtime::Session>, runtime::Session const& session);

  SessionResultSerializer(SessionResultSerializer const&) = delete;
  SessionResultSerializer& operator=(SessionResultSerializer const&) = delete;

  // Each method mirrors one Session member and propagates the underlying
  // session-store / registry / stats / validation error unchanged. state_result_json
  // is infallible and returns the assembled JSON directly.
  [[nodiscard]] std::string state_result_json(bool cancel_requested) const;
  [[nodiscard]] ava::core::Result<std::string> list_sessions_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> session_tree_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> list_models_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> session_stats_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> session_validation_result_json() const;

 private:
  // Serialize `model` against this serializer's locked session, including whether it is configured and currently selected.
  [[nodiscard]] std::string model_info_json(ava::config::ModelInfo const& model, bool configured) const;

  runtime::Session const& session_;

  // References live session content that may carry provider credentials.
  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::rpc::detail
