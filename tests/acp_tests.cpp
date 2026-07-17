#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/acp/client_tools.h"
#include "ava/app/acp/codec.h"
#include "ava/app/acp/content.h"
#include "ava/app/acp/envelope_intent.h"
#include "ava/app/acp/peer.h"
#include "ava/app/acp/permission.h"
#include "ava/app/acp/service.h"
#include "ava/app/acp/session_update.h"
#include "ava/app/acp/transport.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/tool_summaries.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/secure_workspace.h"
#include "ava/mcp/tool_broker.h"
#include "ava/session/attachments.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/registry.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;
using ava::app::acp::JsonRpcId;
namespace runtime = ava::app::runtime;

struct MemoryTransportState
{
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<ava::app::acp::ReadRecord> input;
  std::deque<std::string> output;
  bool eof = false;
  bool canceled = false;
  bool read_waiting = false;
  bool fail_writes = false;
  bool block_writes = false;
  bool write_waiting = false;
  bool publish_before_stall = false;
  std::size_t cancel_calls = 0;
  std::size_t write_attempts = 0;
};

class RecordingShutdownEscalation final : public ava::app::acp::ShutdownEscalation
{
 public:
  explicit RecordingShutdownEscalation(std::atomic_int& calls) : calls_(calls) { }

  [[noreturn]] void escalate() noexcept override
  {
    calls_.fetch_add(1, std::memory_order_relaxed);
    std::_Exit(ava::app::acp::kShutdownEscalationExitCode);
  }

 private:
  std::atomic_int& calls_;
};

class MemoryTransport final : public ava::app::acp::RecordTransport
{
 public:
  // Shared ownership lets the test-side simulated client outlive the transport
  // handle moved into the peer while the peer owns the transport itself.
  explicit MemoryTransport(std::shared_ptr<MemoryTransportState> state) : state_(std::move(state)) { }

  ava::app::acp::ReadRecord read_record() override
  {
    std::unique_lock lock(state_->mutex);
    state_->read_waiting = true;
    state_->cv.notify_all();
    state_->cv.wait(lock, [&] { return state_->canceled || state_->eof || !state_->input.empty(); });
    if (state_->canceled)
      return {.status = ava::app::acp::ReadRecordStatus::Canceled, .record = {}, .diagnostic = {}};
    if (!state_->input.empty())
    {
      auto record = std::move(state_->input.front());
      state_->input.pop_front();
      return record;
    }
    return {.status = ava::app::acp::ReadRecordStatus::EndOfFile, .record = {}, .diagnostic = {}};
  }

  ava::core::VoidResult write_record(std::string const& record) override
  {
    std::unique_lock lock(state_->mutex);
    state_->write_waiting = true;
    ++state_->write_attempts;
    bool const published_before_stall = state_->publish_before_stall;
    if (published_before_stall)
      state_->output.push_back(record);
    state_->cv.notify_all();
    state_->cv.wait(lock, [&] { return !state_->block_writes || state_->canceled; });
    if (state_->fail_writes || (state_->canceled && !published_before_stall))
      return std::unexpected(ava::app::acp::protocol_error("simulated write failure"));
    if (!published_before_stall)
      state_->output.push_back(record);
    state_->cv.notify_all();
    return {};
  }

  void cancel() noexcept override
  {
    std::lock_guard lock(state_->mutex);
    ++state_->cancel_calls;
    state_->canceled = true;
    state_->cv.notify_all();
  }

 private:
  std::shared_ptr<MemoryTransportState> state_;
};

void feed(std::shared_ptr<MemoryTransportState> const& state, std::string record)
{
  std::lock_guard lock(state->mutex);
  state->input.push_back(
      {.status = ava::app::acp::ReadRecordStatus::Record, .record = std::move(record), .diagnostic = {}, .intent = ava::app::acp::EnvelopeIntent::Unknown});
  state->cv.notify_all();
}

void feed_limit_error(std::shared_ptr<MemoryTransportState> const& state, ava::app::acp::EnvelopeIntent intent)
{
  std::lock_guard lock(state->mutex);
  state->input.push_back({.status = ava::app::acp::ReadRecordStatus::RecoverableError, .record = {}, .diagnostic = "record too large", .intent = intent});
  state->cv.notify_all();
}

std::optional<std::string> take_output(std::shared_ptr<MemoryTransportState> const& state, std::chrono::milliseconds timeout = 2s)
{
  std::unique_lock lock(state->mutex);
  if (!state->cv.wait_for(lock, timeout, [&] { return !state->output.empty(); }))
    return std::nullopt;
  auto output = std::move(state->output.front());
  state->output.pop_front();
  return output;
}

void close_input(std::shared_ptr<MemoryTransportState> const& state)
{
  std::lock_guard lock(state->mutex);
  state->eof = true;
  state->cv.notify_all();
}

void wait_reader(std::shared_ptr<MemoryTransportState> const& state)
{
  std::unique_lock lock(state->mutex);
  static_cast<void>(state->cv.wait_for(lock, 2s, [&] { return state->read_waiting; }));
}

void wait_writer(std::shared_ptr<MemoryTransportState> const& state)
{
  std::unique_lock lock(state->mutex);
  static_cast<void>(state->cv.wait_for(lock, 2s, [&] { return state->write_waiting; }));
}

bool wait_for_write_attempts(std::shared_ptr<MemoryTransportState> const& state, std::size_t expected)
{
  std::unique_lock lock(state->mutex);
  return state->cv.wait_for(lock, 2s, [&] { return state->write_attempts >= expected; });
}

bool output_has_code(std::optional<std::string> const& output, int code)
{
  if (!output)
    return false;
  auto decoded = ava::app::acp::decode_message(*output);
  if (!decoded)
    return false;
  auto const* error = std::get_if<ava::app::acp::ErrorResponse>(&*decoded);
  return error != nullptr && error->error.code == code;
}

void configure_acp_test_model(std::filesystem::path const& root)
{
  auto paths = ava::tests::app_test_paths(root);
  std::filesystem::create_directories(paths.ava_config_dir);
  ava::tests::write_app_test_file(
      paths.models_file,
      R"({"default_provider":"moonshot","default_model":"acp-test","models":[{"provider":"moonshot","id":"acp-test","name":"ACP Test","family":"fake","supports_tools":false,"supports_streaming":false,"input_modalities":["text"],"output_modalities":["text"]}]})");
}

ava::app::acp::Request initialize_request(std::int64_t id = 1)
{
  return ava::app::acp::Request{.id = id, .method = "initialize", .params_json = std::string(R"({"protocolVersion":1})")};
}

ava::app::acp::Request initialize_request_with_capabilities(std::string capabilities_json, std::int64_t id = 1)
{
  return ava::app::acp::Request{
      .id = id, .method = "initialize", .params_json = std::string("{\"protocolVersion\":1,\"clientCapabilities\":") + capabilities_json + "}"};
}

class RecordingTransport final : public ava::provider::Transport
{
 public:
  RecordingTransport(std::string* body, std::atomic_bool* entered = nullptr, std::atomic_bool* release = nullptr)
      : body_(body), entered_(entered), release_(release)
  {
  }

  ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    if (body_)
      *body_ = request.body;
    if (entered_)
      entered_->store(true, std::memory_order_release);
    while (release_ && !release_->load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
    return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = R"({"choices":[{"message":{"content":"recorded"},"finish_reason":"stop"}]})"};
  }

 private:
  std::string* body_ = nullptr;
  std::atomic_bool* entered_ = nullptr;
  std::atomic_bool* release_ = nullptr;
};

ava::app::RuntimeProviderRunBundleFactory recording_bundle_factory(std::string* body, std::atomic_bool* entered = nullptr, std::atomic_bool* release = nullptr)
{
  return [body, entered, release](ava::app::runtime::Session const&, ava::app::runtime::RunOptions options,
                                  std::string_view) -> ava::core::Result<ava::app::RuntimeProviderRunBundle> {
    auto provider = ava::provider::builtin_provider_registry().create("moonshot");
    if (!provider)
      return std::unexpected(std::move(provider.error()));
    options.access_token = "test";
    options.stream = false;
    std::unique_ptr<ava::provider::Transport> transport = std::make_unique<RecordingTransport>(body, entered, release);
    std::unique_ptr<ava::provider::Transport> auth = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
    return ava::app::RuntimeProviderRunBundle{
        .provider = std::move(*provider), .transport = std::move(transport), .auth_transport = std::move(auth), .options = std::move(options)};
  };
}

struct CapturingSequenceState
{
  std::mutex mutex;
  std::vector<std::string> request_bodies;
};

class CapturingSequenceTransport final : public ava::provider::Transport
{
 public:
  CapturingSequenceTransport(std::shared_ptr<CapturingSequenceState> state, std::vector<ava::provider::HttpResponse> responses)
      : state_(std::move(state)), responses_(std::move(responses))
  {
  }

  ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    {
      std::lock_guard lock(state_->mutex);
      state_->request_bodies.push_back(request.body);
    }
    if (responses_.empty())
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "capturing transport has no response"));
    auto response = std::move(responses_.front());
    responses_.erase(responses_.begin());
    return response;
  }

 private:
  std::shared_ptr<CapturingSequenceState> state_;
  std::vector<ava::provider::HttpResponse> responses_;
};

ava::app::RuntimeProviderRunBundleFactory sequence_bundle_factory(std::shared_ptr<CapturingSequenceState> state,
                                                                  std::vector<ava::provider::HttpResponse> responses)
{
  return [state = std::move(state), responses = std::move(responses)](ava::app::runtime::Session const&, ava::app::runtime::RunOptions options,
                                                                      std::string_view) -> ava::core::Result<ava::app::RuntimeProviderRunBundle> {
    auto provider = ava::provider::builtin_provider_registry().create("moonshot");
    if (!provider)
      return std::unexpected(std::move(provider.error()));
    options.access_token = "test";
    options.stream = false;
    std::unique_ptr<ava::provider::Transport> transport = std::make_unique<CapturingSequenceTransport>(state, responses);
    std::unique_ptr<ava::provider::Transport> auth = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
    return ava::app::RuntimeProviderRunBundle{
        .provider = std::move(*provider), .transport = std::move(transport), .auth_transport = std::move(auth), .options = std::move(options)};
  };
}

struct RunPhaseBarrier
{
  std::mutex mutex;
  std::condition_variable cv;
  ava::app::RunPhase target = ava::app::RunPhase::AwaitingProvider;
  bool reached = false;
  bool released = false;

  ava::core::VoidResult observe(ava::app::RunPhase phase)
  {
    if (phase != target)
      return {};
    std::unique_lock lock(mutex);
    reached = true;
    cv.notify_all();
    cv.wait(lock, [&] { return released; });
    return {};
  }

  bool wait_until_reached()
  {
    std::unique_lock lock(mutex);
    return cv.wait_for(lock, 2s, [&] { return reached; });
  }

  void release()
  {
    {
      std::lock_guard lock(mutex);
      released = true;
    }
    cv.notify_all();
  }
};

ava::provider::HttpResponse acp_text_response(std::string_view text = "recorded")
{
  return ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = std::string(R"({"choices":[{"message":{"content":")") + ava::core::json::escape(text) + R"("},"finish_reason":"stop"}]})"};
}

std::string read_acp_test_file(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::string text;
  std::getline(file, text);
  return text;
}

void configure_acp_tool_test_model(std::filesystem::path const& root)
{
  auto paths = ava::tests::app_test_paths(root);
  std::filesystem::create_directories(paths.ava_config_dir);
  ava::tests::write_app_test_file(
      paths.models_file,
      R"({"default_provider":"moonshot","default_model":"acp-tools","models":[{"provider":"moonshot","id":"acp-tools","name":"ACP Tools","family":"fake","supports_tools":true,"supports_streaming":false,"input_modalities":["text"],"output_modalities":["text"]}]})");
}

void test_acp_prompt_content_capabilities_and_strict_validation()
{
  using namespace ava::app::acp;
  auto valid = decode_prompt_content(
      R"([{"type":"text","text":"inspect"},{"type":"image","data":"iVBORw0KGgo=","mimeType":"image/png","uri":null},{"type":"resource_link","name":"docs","uri":"https://example.test/docs"}])");
  expect(valid && valid->images.size() == 1 && valid->images.front().bytes.size() == 8 && valid->text.find("not fetched") != std::string::npos,
         "ACP prompt decoder accepts bounded canonical image data and reference-only resource links");

  auto defaulted_optional = decode_prompt_content(
      R"([{"type":"text","text":"defaults","annotations":7,"_meta":[]},{"type":"resource_link","name":"docs","uri":"https://example.test","description":7,"mimeType":{},"title":[],"size":-2},{"type":"image","data":"iVBORw0KGgo=","mimeType":"image/png","uri":{}}])");
  expect(defaulted_optional && defaulted_optional->images.size() == 1 && defaulted_optional->text.find("defaults") != std::string::npos &&
             defaulted_optional->text.find("not fetched") != std::string::npos,
         "ACP prompt decoder defaults malformed optional content fields locally while preserving valid required siblings");

  for (auto const& prompt : {
           R"([{"type":"image","data":"iVBORw0KGgo", "mimeType":"image/png"}])",
           R"([{"type":"image","data":"iVBORw0KGgo=","mimeType":"image/svg+xml"}])",
           R"([{"type":"audio","data":"AAAA","mimeType":"audio/wav"}])",
           R"([{"type":"resource","resource":{"uri":"file:///secret","text":"x"}}])",
           R"([{"type":"future","value":"ignored?"}])",
           R"([{"type":"resource_link","name":"x","uri":7}])",
       })
  {
    auto rejected = decode_prompt_content(prompt);
    expect(!rejected && rejected.error().code == -32602,
           "ACP prompt decoder rejects malformed base64/MIME/discriminators and unsupported rich content with -32602");
  }

  auto image_capabilities = initialize_result_json("1", true);
  auto text_capabilities = initialize_result_json("1", false);
  expect(image_capabilities && image_capabilities->find(R"("image":true)") != std::string::npos && text_capabilities &&
             text_capabilities->find(R"("image":false)") != std::string::npos && image_capabilities->find(R"("loadSession":false)") != std::string::npos &&
             image_capabilities->find(R"("audio":false)") != std::string::npos && image_capabilities->find(R"("embeddedContext":false)") != std::string::npos,
         "ACP initialization derives image support while leaving exact rich-history loading unadvertised");
}

void test_acp_typed_session_update_mapper_ordering_and_limits()
{
  using namespace ava::app::acp;
  using ava::app::EventEnvelope;
  using ava::app::runtime::Event;
  using ava::app::runtime::EventType;
  auto root = std::filesystem::path("/workspace");
  RuntimeSessionUpdateMapper mapper(RuntimeSessionUpdateMapperOptions{.workspace_root = root, .message_id = "message_1"});
  runtime::Event text;
  text.type = runtime::EventType::MessageUpdate;
  text.text = "hello";
  runtime::Event final;
  final.type = runtime::EventType::AssistantMessage;
  final.text = "hello";
  runtime::Event thought;
  thought.type = runtime::EventType::ReasoningDelta;
  thought.text = "considering";
  runtime::Event start;
  start.type = runtime::EventType::ToolStart;
  start.call_id = "call_1";
  start.tool_name = "write_file";
  start.tool_arguments_json = R"({"path":"src/a.cpp"})";
  start.text = "src/a.cpp";
  runtime::Event progress;
  progress.type = runtime::EventType::ToolProgress;
  progress.text = "writing";
  progress.call_id = "call_1";
  progress.tool_name = "write_file";
  progress.status = "running";
  runtime::Event result;
  result.type = runtime::EventType::ToolResult;
  result.call_id = "call_1";
  result.tool_name = "write_file";
  result.tool_result_json = R"({"ok":true})";
  result.status = "success";
  result.changed_paths = {"src/a.cpp"};
  auto text_update = mapper.map_and_encode(text);
  auto duplicate = mapper.map_and_encode(final);
  auto thought_update = mapper.map_and_encode(thought);
  auto start_update = mapper.map_and_encode(start);
  auto progress_update = mapper.map_and_encode(progress);
  auto result_update = mapper.map_and_encode(result);
  expect(text_update && *text_update && (*text_update)->find("agent_message_chunk") != std::string::npos && duplicate && !*duplicate && thought_update &&
             *thought_update && (*thought_update)->find("agent_thought_chunk") != std::string::npos && start_update && *start_update &&
             (*start_update)->find(R"("sessionUpdate":"tool_call")") != std::string::npos && (*start_update)->find(R"("kind":"edit")") != std::string::npos &&
             (*start_update)->find(R"("status":"pending")") != std::string::npos && (*start_update)->find("/workspace/src/a.cpp") != std::string::npos &&
             progress_update && *progress_update && (*progress_update)->find(R"("status":"in_progress")") != std::string::npos && result_update &&
             *result_update && (*result_update)->find(R"("status":"completed")") != std::string::npos,
         "typed ACP mapper preserves text/thought/tool ordering, stable ids, content, status, kind, locations, and final de-duplication");

  RuntimeSessionUpdateMapper envelope_mapper(RuntimeSessionUpdateMapperOptions{.workspace_root = root, .message_id = "message_2"});
  EventEnvelope envelope;
  envelope.timestamp = "now";
  envelope.session_id = "session";
  envelope.name = "tool_progress";
  envelope.payload_json = R"({"call_id":"call_2","tool":"bash","text":"running","status":"running"})";
  envelope.payload_type = "tool";
  auto envelope_update = envelope_mapper.map_and_encode(envelope);
  expect(envelope_update && *envelope_update && (*envelope_update)->find(R"("kind":"execute")") != std::string::npos,
         "typed ACP mapper accepts the protocol-neutral EventEnvelope seam without leaking unknown events");

  RuntimeSessionUpdateMapper bounded(
      RuntimeSessionUpdateMapperOptions{.workspace_root = root, .message_id = "bounded", .max_updates = 2, .max_encoded_bytes = 4096});
  runtime::Event bounded_one;
  bounded_one.type = runtime::EventType::MessageUpdate;
  bounded_one.text = "1";
  runtime::Event bounded_two;
  bounded_two.type = runtime::EventType::ReasoningDelta;
  bounded_two.text = "2";
  runtime::Event bounded_three;
  bounded_three.type = runtime::EventType::ToolProgress;
  bounded_three.text = "3";
  bounded_three.call_id = "call";
  bounded_three.tool_name = "bash";
  auto one = bounded.map_and_encode(bounded_one);
  auto two = bounded.map_and_encode(bounded_two);
  auto saturated = bounded.map_and_encode(bounded_three);
  expect(one && *one && two && *two && !saturated && saturated.error().message().find("budget") != std::string::npos,
         "ACP mapper fails closed when the per-prompt update budget saturates");

  RuntimeSessionUpdateMapper coalesced(
      RuntimeSessionUpdateMapperOptions{.workspace_root = root, .message_id = "coalesced", .max_updates = 8, .max_encoded_bytes = 64 * 1024});
  std::vector<std::string> coalesced_updates;
  bool coalesce_failed = false;
  for (std::size_t index = 0; index < 5'000; ++index)
  {
    runtime::Event delta;
    delta.type = runtime::EventType::MessageUpdate;
    delta.text = "x";
    auto batch = coalesced.map_coalesced_and_encode(delta);
    if (!batch)
      coalesce_failed = true;
    else
      coalesced_updates.insert(coalesced_updates.end(), std::make_move_iterator(batch->begin()), std::make_move_iterator(batch->end()));
  }
  runtime::Event done;
  done.type = runtime::EventType::Done;
  auto flushed = coalesced.map_coalesced_and_encode(done);
  if (flushed)
    coalesced_updates.insert(coalesced_updates.end(), std::make_move_iterator(flushed->begin()), std::make_move_iterator(flushed->end()));
  std::size_t coalesced_text_bytes = 0;
  for (auto const& update : coalesced_updates)
  {
    auto content = ava::core::json::object_field(update, "content");
    if (content)
      coalesced_text_bytes += ava::core::json::string_field(*content, "text").value_or("").size();
  }
  expect(!coalesce_failed && flushed && coalesced_updates.size() == 5 && coalesced_text_bytes == 5'000 && coalesced.update_count() == 5,
         "ACP mapper coalesces provider-fragmented adjacent text into bounded live chunks before applying update-count and byte budgets");

  RuntimeSessionUpdateMapper unicode_mapper(
      RuntimeSessionUpdateMapperOptions{.workspace_root = root, .message_id = "unicode", .max_updates = 4, .max_encoded_bytes = 16 * 1024});
  runtime::Event unicode_delta;
  unicode_delta.type = runtime::EventType::MessageUpdate;
  unicode_delta.text = std::string(kMaxStreamContentChunkBytes - 1, 'a') + "€x";
  auto unicode_batch = unicode_mapper.map_coalesced_and_encode(unicode_delta);
  auto unicode_flush = unicode_mapper.flush_coalesced();
  std::string reconstructed;
  std::size_t unicode_updates = 0;
  if (unicode_batch && unicode_flush)
  {
    unicode_batch->insert(unicode_batch->end(), std::make_move_iterator(unicode_flush->begin()), std::make_move_iterator(unicode_flush->end()));
    unicode_updates = unicode_batch->size();
    for (auto const& update : *unicode_batch)
      if (auto content = ava::core::json::object_field(update, "content"))
        reconstructed += ava::core::json::string_field(*content, "text").value_or("");
  }
  expect(unicode_batch && unicode_flush && unicode_updates == 2 && reconstructed == unicode_delta.text,
         "ACP stream coalescing preserves a multibyte UTF-8 code point that crosses the byte chunk boundary");
}

void test_acp_permission_dtos_show_bounded_actions()
{
  using namespace ava::app::acp;
  ava::permissions::PermissionPrompt prompt;
  prompt.permission_request_id = "perm_1";
  prompt.tool_call_id = "call_1";
  prompt.operation = ava::permissions::Operation::RunCommand;
  prompt.mode = ava::agent::Mode::Build;
  prompt.workspace_dir = "/workspace";
  prompt.target_path = "/workspace";
  prompt.command = "secret-command --token value";
  prompt.tool_name = "bash";
  prompt.reason = "command approval";
  prompt.risk = ava::permissions::PermissionRisk::High;
  auto params = encode_permission_request_params("session_1", prompt, "/workspace");
  expect(params && params->find(R"("sessionId":"session_1")") != std::string::npos && params->find(R"("toolCallId":"call_1")") != std::string::npos &&
             params->find(R"("kind":"execute")") != std::string::npos && params->find(R"("optionId":"allow_once")") != std::string::npos &&
             params->find("secret-command --token value") != std::string::npos && params->find("exact request for this session") != std::string::npos,
         "ACP permission request links identity and exposes the bounded exact command before offering a session grant");

  prompt.command.assign(7U * 1024U, 'x');
  auto truncated_command = encode_permission_request_params("session_1", prompt, "/workspace");
  expect(truncated_command && truncated_command->find("command display truncated") != std::string::npos &&
             truncated_command->find(R"("optionId":"allow_once")") != std::string::npos &&
             truncated_command->find(R"("optionId":"allow_always")") == std::string::npos &&
             truncated_command->find(R"("optionId":"reject_always")") == std::string::npos && !permission_request_offers_session_decisions(prompt),
         "ACP hides session-wide decisions when a command cannot be displayed completely within the bounded permission request");

  prompt.operation = ava::permissions::Operation::EditFile;
  prompt.tool_name = "edit_file";
  prompt.command.clear();
  prompt.target_path = "/workspace/src/main.cpp";
  prompt.diff_preview = "--- a/src/main.cpp\n+++ b/src/main.cpp\n@@\n-old\n+new";
  auto edit_params = encode_permission_request_params("session_1", prompt, "/workspace");
  expect(edit_params && edit_params->find("Proposed file diff") != std::string::npos && edit_params->find("+new") != std::string::npos &&
             edit_params->find(R"("optionId":"allow_once")") != std::string::npos && edit_params->find(R"("optionId":"reject_once")") != std::string::npos &&
             edit_params->find(R"("optionId":"allow_always")") == std::string::npos && edit_params->find(R"("optionId":"reject_always")") == std::string::npos,
         "ACP file-mutation permission shows a bounded diff and offers only one-shot decisions");

  auto selected = decode_permission_response(R"({"outcome":{"outcome":"selected","optionId":"allow_always"}})");
  auto cancelled = decode_permission_response(R"({"outcome":{"outcome":"cancelled"}})");
  expect(selected && *selected == AcpPermissionSelection::AllowAlways && cancelled && *cancelled == AcpPermissionSelection::Cancelled,
         "ACP permission response DTO strictly parses pinned Selected and Cancelled outcomes");
  auto selected_with_invalid_response_meta = decode_permission_response(R"({"_meta":[],"outcome":{"outcome":"selected","optionId":"allow_once"}})");
  auto selected_with_invalid_outcome_meta = decode_permission_response(R"({"outcome":{"outcome":"selected","optionId":"reject_once","_meta":"invalid"}})");
  expect(selected_with_invalid_response_meta && *selected_with_invalid_response_meta == AcpPermissionSelection::AllowOnce &&
             selected_with_invalid_outcome_meta && *selected_with_invalid_outcome_meta == AcpPermissionSelection::RejectOnce,
         "ACP permission responses default malformed optional _meta fields locally as required by the pinned schema");
  for (auto const& malformed : {R"({"outcome":{"outcome":"selected","optionId":"wildcard"}})", R"({"outcome":{"outcome":"cancelled","optionId":"allow_once"}})",
                                R"({"outcome":"selected"})", R"({"outcome":{"outcome":"future"}})"})
  {
    auto rejected = decode_permission_response(malformed);
    expect(!rejected && rejected.error().code == -32602, "ACP permission response parser rejects invalid outcomes and unoffered option ids");
  }
}

void test_acp_codec_envelopes_and_ids()
{
  using namespace ava::app::acp;
  auto integer = decode_message(R"({"jsonrpc":"2.0","id":7,"method":"x","params":{}})");
  auto string = decode_message(R"({"jsonrpc":"2.0","id":"7","method":"x","params":[]})");
  expect(integer && std::holds_alternative<Request>(*integer) && std::get<std::int64_t>(std::get<Request>(*integer).id) == 7,
         "ACP codec preserves integer request ids");
  expect(string && std::holds_alternative<Request>(*string) && std::get<std::string>(std::get<Request>(*string).id) == "7",
         "ACP codec preserves string request ids");

  auto null_request = decode_message(R"({"jsonrpc":"2.0","id":null,"method":"x"})");
  expect(null_request && std::holds_alternative<Request>(*null_request) && std::holds_alternative<NullJsonRpcId>(std::get<Request>(*null_request).id),
         "ACP codec preserves a present null request id instead of treating it as a notification");
  auto null_params = decode_message(R"({"jsonrpc":"2.0","id":8,"method":"session/list","params":null})");
  expect(null_params && std::holds_alternative<Request>(*null_params) && !std::get<Request>(*null_params).params_json,
         "ACP codec accepts params:null and normalizes it to absent parameters");
  auto notification = decode_message(R"({"jsonrpc":"2.0","method":"session/cancel","params":{"sessionId":"s"}})");
  auto response = decode_message(R"({"jsonrpc":"2.0","id":7,"result":{"ok":true}})");
  auto error = decode_message(R"({"jsonrpc":"2.0","id":"7","error":{"code":-32800,"message":"cancelled","data":{"safe":true}}})");
  expect(notification && std::holds_alternative<Notification>(*notification), "ACP codec routes notifications");
  expect(response && std::holds_alternative<Response>(*response), "ACP codec routes result responses");
  expect(error && std::holds_alternative<ErrorResponse>(*error) && std::get<ErrorResponse>(*error).error.data_json.has_value(),
         "ACP codec routes typed error responses and preserves data");

  auto null_success = encode_success(JsonRpcId(NullJsonRpcId{}), R"({"ok":true})");
  auto null_error = encode_error(JsonRpcId(NullJsonRpcId{}), -32603, "failed");
  expect(null_success && null_success->find("\"id\":null") != std::string::npos && null_error && null_error->find("\"id\":null") != std::string::npos,
         "ACP success and error serialization preserve explicit null ids");

  for (auto const& bad :
       {R"({"jsonrpc":"2.0","id":1.5,"method":"x"})", R"({"jsonrpc":"2.0","id":true,"method":"x"})", R"({"jsonrpc":"2.0","id":{},"method":"x"})"})
  {
    auto decoded = decode_message(bad);
    expect(!decoded && decoded.error().code == -32600, "ACP codec rejects unsupported JSON-RPC id types");
  }
  auto wrong_version = decode_message(R"({"jsonrpc":"1.0","id":1,"method":"x"})");
  auto wrong_params = decode_message(R"({"jsonrpc":"2.0","id":1,"method":"x","params":"bad"})");
  auto exclusive = decode_message(R"({"jsonrpc":"2.0","id":1,"result":{},"error":{"code":1,"message":"x"}})");
  auto id_only = decode_message(R"({"jsonrpc":"2.0","id":1})");
  auto null_id_only = decode_message(R"({"jsonrpc":"2.0","id":null})");
  auto malformed_error = decode_message(R"({"jsonrpc":"2.0","id":1,"error":{"code":"bad","message":1}})");
  auto batch = decode_message(R"([{"jsonrpc":"2.0","id":1,"method":"x"}])");
  expect(!wrong_version && wrong_version.error().code == -32600, "ACP codec requires jsonrpc 2.0");
  expect(!wrong_params && wrong_params.error().code == -32600, "ACP codec requires structured params");
  expect(!exclusive && exclusive.error().code == -32600 && exclusive.error().intent == EnvelopeIntent::Response && exclusive.error().suppress_response,
         "ACP codec classifies malformed response envelopes before reply policy");
  expect(!id_only && id_only.error().intent == EnvelopeIntent::Response && id_only.error().suppress_response && !null_id_only &&
             null_id_only.error().intent == EnvelopeIntent::Response && null_id_only.error().suppress_response && !malformed_error &&
             malformed_error.error().intent == EnvelopeIntent::Response && malformed_error.error().suppress_response,
         "ACP codec treats every id-bearing methodless object as response intent, including null ids and malformed response bodies");
  expect(!batch && batch.error().code == -32600, "ACP codec explicitly rejects batches");

  for (auto const& duplicate : {R"({"jsonrpc":"2.0","id":1,"id":2,"method":"x"})", R"({"jsonrpc":"2.0","id":1,"method":"x","\u006dethod":"y"})",
                                R"({"jsonrpc":"2.0","id":1,"method":"x","params":{"future":1,"future":2}})"})
  {
    auto rejected = decode_message(duplicate);
    expect(!rejected && rejected.error().code == -32600, "ACP rejects duplicate member names, including escaped-equivalent and nested keys");
  }
  auto additive = decode_message(R"({"jsonrpc":"2.0","id":1,"method":"x","future":{"unique":true}})");
  expect(additive && std::holds_alternative<Request>(*additive), "ACP continues accepting unique additive fields");

  auto escaped_response = scan_envelope_intent(R"({"payload":"escaped quote: \" and fake braces: {[","i\u0064":null,"result":[{"method":"nested"}]})");
  auto trailing_request = scan_envelope_intent(R"({"params":{"nested":[{"id":99,"method":"nested"}]},"m\u0065thod":"top-level","id":7})");
  auto crossed_response = scan_envelope_intent(R"({"id":8,"result":[{"x":0],"method":"not-reliably-top-level"})");
  expect(escaped_response.intent == EnvelopeIntent::Response && trailing_request.intent == EnvelopeIntent::Request &&
             loop_safe_oversized_intent(crossed_response) == EnvelopeIntent::Response,
         "streaming envelope scan handles escaped keys and strings, ignores nested envelope names, and stays loop-safe on mismatched nesting");
}

void test_acp_codec_initialize_meta_additive_and_malformed_fields()
{
  using namespace ava::app::acp;
  auto decoded = decode_message(
      R"({"jsonrpc":"2.0","id":"init","method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{"fs":{"readTextFile":true,"future":1,"_meta":{"fs":1}},"terminal":true,"session":{"configOptions":{"boolean":{"_meta":{"boolean":1}},"_meta":{"config":1}},"_meta":{"session":1}},"_meta":{"cap":1}},"clientInfo":{"name":"client","version":"2","title":"Client","_meta":{"i":2}},"_meta":{"root":3},"futureField":{"anything":true}}})");
  expect(decoded && std::holds_alternative<Request>(*decoded), "ACP initialize envelope parses");
  if (!decoded || !std::holds_alternative<Request>(*decoded))
    return;
  auto initialize = decode_initialize_params(std::get<Request>(*decoded));
  expect(initialize && initialize->protocol_version == 1 && initialize->client_capabilities.read_text_file && initialize->client_capabilities.terminal &&
             initialize->client_capabilities.boolean_config_options && initialize->client_info && initialize->meta_json && initialize->client_info->meta_json &&
             initialize->client_capabilities.meta_json && initialize->client_capabilities.fs_meta_json && initialize->client_capabilities.session_meta_json &&
             initialize->client_capabilities.config_options_meta_json && initialize->client_capabilities.boolean_config_meta_json,
         "ACP initialize accepts additive fields and preserves all recognized _meta objects");

  for (auto const& params : {R"({})", R"({"protocolVersion":"1"})", R"({"protocolVersion":-1})", R"({"protocolVersion":65536})"})
  {
    Request request{.id = std::int64_t(1), .method = "initialize", .params_json = std::string(params)};
    auto malformed = decode_initialize_params(request);
    expect(!malformed && malformed.error().code == -32602, "ACP initialize keeps required protocolVersion strict");
  }

  auto decode_params = [](std::string params) {
    return decode_initialize_params(Request{.id = std::int64_t(1), .method = "initialize", .params_json = std::move(params)});
  };
  auto malformed_capabilities = decode_params(R"({"protocolVersion":1,"clientCapabilities":[]})");
  expect(malformed_capabilities && !malformed_capabilities->client_capabilities.read_text_file &&
             !malformed_capabilities->client_capabilities.write_text_file && !malformed_capabilities->client_capabilities.terminal &&
             !malformed_capabilities->client_capabilities.boolean_config_options,
         "malformed clientCapabilities defaults to the all-false capability object");

  auto field_local = decode_params(
      R"({"protocolVersion":1,"clientCapabilities":{"fs":{"readTextFile":true,"writeTextFile":"bad","_meta":[]},"terminal":"bad","session":{"configOptions":{"boolean":[],"_meta":[]},"_meta":[]},"_meta":[]},"clientInfo":{"name":"client","version":"2","title":7,"_meta":[]},"_meta":[]})");
  expect(field_local && field_local->client_capabilities.read_text_file && !field_local->client_capabilities.write_text_file &&
             !field_local->client_capabilities.terminal && !field_local->client_capabilities.boolean_config_options &&
             !field_local->client_capabilities.meta_json && !field_local->client_capabilities.fs_meta_json &&
             !field_local->client_capabilities.session_meta_json && !field_local->client_capabilities.config_options_meta_json && field_local->client_info &&
             field_local->client_info->name == "client" && field_local->client_info->version == "2" && !field_local->client_info->title &&
             !field_local->client_info->meta_json && !field_local->meta_json,
         "initialize defaults malformed optional fields locally while preserving valid required and sibling fields");

  auto malformed_fs = decode_params(R"({"protocolVersion":1,"clientCapabilities":{"fs":[],"terminal":true,"session":{"configOptions":{"boolean":{}}}}})");
  expect(malformed_fs && !malformed_fs->client_capabilities.read_text_file && !malformed_fs->client_capabilities.write_text_file &&
             malformed_fs->client_capabilities.terminal && malformed_fs->client_capabilities.boolean_config_options,
         "malformed fs defaults both filesystem flags without discarding valid terminal or session siblings");

  auto write_sibling = decode_params(
      R"({"protocolVersion":1,"clientCapabilities":{"fs":{"readTextFile":{},"writeTextFile":true},"session":[]},"clientInfo":{"name":"x","version":2}})");
  expect(write_sibling && !write_sibling->client_capabilities.read_text_file && write_sibling->client_capabilities.write_text_file &&
             !write_sibling->client_capabilities.boolean_config_options && !write_sibling->client_info,
         "malformed individual filesystem booleans and optional capability/clientInfo objects default absent without erasing valid siblings");

  auto malformed_config = decode_params(R"({"protocolVersion":1,"clientCapabilities":{"session":{"configOptions":[]}},"clientInfo":"bad"})");
  expect(malformed_config && !malformed_config->client_capabilities.boolean_config_options && !malformed_config->client_info,
         "malformed optional config capabilities and clientInfo default absent");

  auto result = encode_initialize_result(std::string("id"), "1.0.0", true);
  expect(result && result->find("\"protocolVersion\":1") != std::string::npos,
         "ACP initialize result codec remains testable independently of the M2 public conformance gate");
}

void test_acp_codec_utf8_depth_size_and_serialization()
{
  using namespace ava::app::acp;
  std::string invalid = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"";
  invalid.push_back(static_cast<char>(0xFF));
  invalid += "\"}";
  auto invalid_utf8 = decode_message(invalid);
  expect(!invalid_utf8 && invalid_utf8.error().code == -32700, "invalid inbound UTF-8 produces a parse error");
  auto encoded_error = encode_error(std::nullopt, -32700, "Parse error");
  expect(encoded_error && encoded_error->find(static_cast<char>(0xFF)) == std::string::npos, "ACP error serialization never emits invalid inbound bytes");

  std::string deep = R"({"jsonrpc":"2.0","id":1,"method":"x","params":)";
  deep.append(kMaxNestingDepth + 1, '[');
  deep.append(kMaxNestingDepth + 1, ']');
  deep += '}';
  auto too_deep = decode_message(deep);
  expect(!too_deep && too_deep.error().code == -32700, "ACP codec bounds nesting depth");

  std::string deep_value(kMaxNestingDepth + 1, '[');
  deep_value += '0';
  deep_value.append(kMaxNestingDepth + 1, ']');
  auto deep_integer_response = decode_message(R"({"jsonrpc":"2.0","id":41,"result":)" + deep_value + '}');
  auto deep_null_response = decode_message(R"({"jsonrpc":"2.0","result":)" + deep_value + R"(,"id":null})");
  expect(!deep_integer_response && deep_integer_response.error().intent == EnvelopeIntent::Response && deep_integer_response.error().suppress_response &&
             !deep_null_response && deep_null_response.error().intent == EnvelopeIntent::Response && deep_null_response.error().suppress_response,
         "ACP codec establishes integer and null response intent before depth rejection, including an id after the deep value");

  auto too_large = decode_message(std::string(kMaxRecordBytes + 1, ' '));
  expect(!too_large && too_large.error().code == -32700, "ACP codec bounds record bytes before parsing");

  std::string long_method(kMaxMethodBytes + 1, 'x');
  auto long_record = encode_request(std::int64_t(1), long_method, std::nullopt);
  expect(!long_record, "ACP serialization bounds method length");
  auto malformed_result = encode_success(std::int64_t(1), "{bad");
  expect(!malformed_result, "ACP serialization rejects malformed raw JSON at the codec boundary");
  std::string oversized_string = R"({"jsonrpc":"2.0","id":1,"method":"x","params":{"value":")";
  oversized_string.append(kMaxStringBytes + 1, 'x');
  oversized_string += R"("}})";
  auto too_long = decode_message(oversized_string);
  expect(!too_long && too_long.error().code == -32700, "ACP codec bounds individual JSON strings");

  std::string collection = R"({"jsonrpc":"2.0","id":1,"method":"x","params":[)";
  for (std::size_t index = 0; index <= kMaxCollectionItems; ++index)
  {
    if (index != 0)
      collection += ',';
    collection += "0";
  }
  collection += "]}";
  auto too_many = decode_message(collection);
  expect(!too_many && too_many.error().code == -32700, "ACP codec bounds JSON collection sizes");
}

void test_acp_transport_lf_crlf_and_final_record()
{
  using namespace ava::app::acp;
  int input[2] = {-1, -1};
  int output[2] = {-1, -1};
  bool const opened = pipe(input) == 0 && pipe(output) == 0;
  expect(opened, "ACP framing test creates transport pipes");
  if (!opened)
    return;
  auto transport = make_fd_record_transport(input[0], output[1]);
  expect(transport.has_value(), "ACP framing test creates fd transport");
  if (!transport)
    return;
  std::string const records = "{}\r\n[]\n{\"final\":true}";
  auto const count = write(input[1], records.data(), records.size());
  static_cast<void>(close(input[1]));
  auto crlf = (*transport)->read_record();
  auto lf = (*transport)->read_record();
  auto final = (*transport)->read_record();
  auto eof = (*transport)->read_record();
  expect(count == static_cast<ssize_t>(records.size()) && crlf.status == ReadRecordStatus::Record && crlf.record == "{}" &&
             lf.status == ReadRecordStatus::Record && lf.record == "[]" && final.status == ReadRecordStatus::Record && final.record == R"({"final":true})" &&
             eof.status == ReadRecordStatus::EndOfFile,
         "ACP transport accepts CRLF, LF, and a bounded unterminated final record");
  transport->reset();
  static_cast<void>(close(input[0]));
  static_cast<void>(close(output[0]));
  static_cast<void>(close(output[1]));
}

void test_acp_service_gating_reinitialize_and_negotiation()
{
  using namespace ava::app::acp;
  AgentService service("1.0.0");
  Request before{.id = std::int64_t(1), .method = "session/new", .params_json = std::string("{}")};
  auto preinit = service.handle_request(before, {});
  expect(!preinit && preinit.error().code == -32600, "ACP service rejects methods before initialize");

  Request initialize{.id = std::int64_t(2), .method = "initialize", .params_json = std::string(R"({"protocolVersion":99})")};
  auto initialized = service.handle_request(initialize, {});
  expect(initialized && initialized->find("\"protocolVersion\":1") != std::string::npos && initialized->find("\"loadSession\":false") != std::string::npos &&
             initialized->find("\"image\":true") != std::string::npos && service.initialized(),
         "ACP M4 initializes successfully with the immutable truthful capability matrix");
  auto again = service.handle_request(initialize, {});
  expect(!again && again.error().code == -32600, "ACP initialize remains single-shot");
  auto unknown = service.handle_request(Request{.id = std::int64_t(3), .method = "unknown", .params_json = std::string("{}")}, {});
  expect(!unknown && unknown.error().code == -32601, "ACP initialized service returns method-not-found for unadvertised operations");
}

void test_acp_service_mutating_request_terminal_commits()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-service-commit");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  std::set<std::int64_t> canceled_ids{1, 2, 4, 6};
  AgentService service(options);
  service.bind_request_terminal_committer([&canceled_ids](JsonRpcId const& id) {
    auto const* value = std::get_if<std::int64_t>(&id);
    return value == nullptr || !canceled_ids.contains(*value);
  });

  auto canceled_initialize = service.handle_request(initialize_request(1), {});
  expect(!canceled_initialize && canceled_initialize.error().code == -32800 && !service.initialized(),
         "cancellation before initialize commit leaves the service uninitialized");
  auto initialized = service.handle_request(initialize_request(10), {});
  expect(initialized && service.initialized(), "a committed initialize publishes the validated initialized state");

  auto omitted_list = service.handle_request(Request{.id = std::int64_t(11), .method = "session/list", .params_json = std::nullopt}, {});
  auto null_list = service.handle_request(Request{.id = std::int64_t(12), .method = "session/list", .params_json = std::string("null")}, {});
  expect(omitted_list && null_list, "session/list treats omitted and null params as the pinned empty object");
  auto omitted_new = service.handle_request(Request{.id = std::int64_t(13), .method = "session/new", .params_json = std::nullopt}, {});
  auto null_new = service.handle_request(Request{.id = std::int64_t(14), .method = "session/new", .params_json = std::string("null")}, {});
  expect(!omitted_new && omitted_new.error().code == -32602 && !null_new && null_new.error().code == -32602,
         "required-parameter methods still reject omitted and null params at method level");

  auto canceled_new = service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto sessions_after_cancel = ava::session::SessionStore::list_sessions(workspace, paths.sessions_dir);
  expect(!canceled_new && canceled_new.error().code == -32800 && sessions_after_cancel && sessions_after_cancel->empty(),
         "cancellation before session/new commit creates no persistent or registry session");

  auto created = service.handle_request(
      Request{.id = std::int64_t(3), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  expect(session_id.has_value(), "a committed session/new performs the registry mutation and returns its session id");
  if (session_id)
  {
    auto canceled_close = service.handle_request(
        Request{.id = std::int64_t(4), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *session_id + "\"}"}, {});
    auto committed_close = service.handle_request(
        Request{.id = std::int64_t(5), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *session_id + "\"}"}, {});
    expect(!canceled_close && canceled_close.error().code == -32800 && committed_close,
           "a canceled close leaves the host active and a committed close removes it");

    auto canceled_resume = service.handle_request(
        Request{.id = std::int64_t(6),
                .method = "session/resume",
                .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"cwd\":\"" + workspace.string() + "\",\"mcpServers\":[]}"},
        {});
    auto committed_resume = service.handle_request(
        Request{.id = std::int64_t(7),
                .method = "session/resume",
                .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"cwd\":\"" + workspace.string() + "\",\"mcpServers\":[]}"},
        {});
    expect(!canceled_resume && canceled_resume.error().code == -32800 && committed_resume,
           "a canceled resume inserts no host and a committed resume remains available");
    static_cast<void>(service.handle_request(
        Request{.id = std::int64_t(8), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *session_id + "\"}"}, {}));
  }

  service.shutdown();
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_session_request_schema_defaults_and_invalid_item_skipping()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-schema-defaults");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  AgentService service(options);
  static_cast<void>(service.handle_request(initialize_request(), {}));

  auto create = [&](std::int64_t id, std::string fields) {
    return service.handle_request(
        Request{.id = id, .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\"" + std::move(fields) + "}"}, {});
  };
  auto missing_mcp = create(2, R"(,"_meta":[])");
  auto malformed_collections = create(3, R"(,"additionalDirectories":7,"mcpServers":{})");
  auto invalid_items = create(4, R"(,"additionalDirectories":[7,{},null],"mcpServers":[7,{"name":4},{"name":"bad","command":"relative","args":[],"env":[]}])");
  std::string many_invalid_items = R"(,"mcpServers":[)";
  for (std::size_t index = 0; index < kMaxConnectionSessions; ++index)
  {
    if (index != 0)
      many_invalid_items.push_back(',');
    many_invalid_items += "null";
  }
  many_invalid_items += "]";
  auto skipped_before_bound = create(5, std::move(many_invalid_items));
  auto valid_directory = create(6, R"(,"additionalDirectories":["/other"],"mcpServers":[])");
  auto valid_http = create(7, R"(,"mcpServers":[{"type":"http","name":"remote","url":"https://example.test/mcp","headers":[]}])");

  std::vector<std::string> created_ids;
  for (auto const* result : {&missing_mcp, &malformed_collections, &invalid_items, &skipped_before_bound})
    if (*result)
      if (auto id = ava::core::json::string_field(**result, "sessionId"))
        created_ids.push_back(std::move(*id));
  expect(
      created_ids.size() == 4 && !valid_directory && valid_directory.error().message.find("additionalDirectories") != std::string::npos && !valid_http &&
          valid_http.error().message.find("implicit ACP stdio") != std::string::npos,
      "ACP applies field-local defaults and invalid-item skipping before count bounds or rejection of normalized valid unsupported roots and MCP transports");

  for (auto const& id : created_ids)
    static_cast<void>(
        service.handle_request(Request{.id = std::int64_t(20), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + id + "\"}"}, {}));
  service.shutdown();
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_session_capacity_is_reserved_before_persistence()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-session-capacity");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  AgentService service(options);
  static_cast<void>(service.handle_request(initialize_request(), {}));

  std::vector<std::string> session_ids;
  for (std::size_t index = 0; index < kMaxConnectionSessions; ++index)
  {
    auto created = service.handle_request(Request{.id = static_cast<std::int64_t>(index + 2),
                                                  .method = "session/new",
                                                  .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"},
                                          {});
    if (created)
      if (auto id = ava::core::json::string_field(*created, "sessionId"))
        session_ids.push_back(std::move(*id));
  }
  auto rejected = service.handle_request(
      Request{.id = std::int64_t(100), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto persisted = ava::session::SessionStore::list_sessions(workspace, paths.sessions_dir);
  expect(session_ids.size() == kMaxConnectionSessions && !rejected && rejected.error().message.find("session limit") != std::string::npos && persisted &&
             persisted->size() == kMaxConnectionSessions,
         "ACP reserves connection capacity before session/new creates durable state and leaves no inaccessible overflow session");

  for (auto const& id : session_ids)
    static_cast<void>(
        service.handle_request(Request{.id = std::int64_t(200), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + id + "\"}"}, {}));
  service.shutdown();
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_startup_model_is_pinned_across_config_mutation()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-model-pin");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);

  std::string request_body;
  std::vector<ava::config::ModelInfo> observed_models;
  auto base_factory = recording_bundle_factory(&request_body);
  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = [&observed_models, base_factory](ava::app::runtime::Session const& session, ava::app::runtime::RunOptions run_options,
                                                                     std::string_view label) mutable {
    observed_models.push_back(session.model);
    return base_factory(session, std::move(run_options), label);
  };
  AgentService service(options);
  service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  auto initialized = service.handle_request(initialize_request(), {});
  expect(initialized && initialized->find(R"("image":false)") != std::string::npos, "ACP initialize advertises capability from the startup model snapshot");

  ava::tests::write_app_test_file(
      paths.models_file,
      R"({"default_provider":"moonshot","default_model":"mutated-image","models":[{"provider":"moonshot","id":"mutated-image","name":"Mutated","family":"fake","supports_tools":false,"supports_streaming":false,"input_modalities":["text","image"],"output_modalities":["text"]}]})");
  auto created = service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  expect(session_id.has_value(), "ACP creates a session after model config mutation");
  if (!session_id)
    return;
  auto prompted =
      service.handle_request(Request{.id = std::int64_t(3),
                                     .method = "session/prompt",
                                     .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"pin\"}]}"},
                             {});
  expect(prompted && observed_models.size() == 1 && observed_models.front().provider_id == "moonshot" && observed_models.front().model_id == "acp-test" &&
             std::ranges::find(observed_models.front().input_modalities, "image") == observed_models.front().input_modalities.end(),
         "ACP session/new uses the exact startup model despite later config edits");

  static_cast<void>(service.handle_request(
      Request{.id = std::int64_t(4), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *session_id + "\"}"}, {}));
  auto resumed = service.handle_request(
      Request{.id = std::int64_t(5),
              .method = "session/resume",
              .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"cwd\":\"" + workspace.string() + "\",\"mcpServers\":[]}"},
      {});
  auto resumed_prompt = service.handle_request(
      Request{.id = std::int64_t(6),
              .method = "session/prompt",
              .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"resume\"}]}"},
      {});
  expect(resumed && resumed_prompt && observed_models.size() == 2 && observed_models.back().model_id == "acp-test" &&
             std::ranges::find(observed_models.back().input_modalities, "image") == observed_models.back().input_modalities.end(),
         "ACP session/resume keeps the same immutable startup model and capabilities");

  auto const invalid_root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-model-unresolved");
  std::filesystem::create_directories(invalid_root / "workspace");
  auto invalid_paths = ava::tests::app_test_paths(invalid_root);
  std::filesystem::create_directories(invalid_paths.ava_config_dir);
  ava::tests::write_app_test_file(invalid_paths.models_file, R"({"default_provider":"missing","default_model":"missing"})");
  AgentServiceOptions invalid_options;
  invalid_options.agent_version = "1";
  invalid_options.launch_root = std::filesystem::canonical(invalid_root / "workspace");
  invalid_options.paths = invalid_paths;
  AgentService invalid_service(invalid_options);
  auto unresolved = invalid_service.handle_request(initialize_request(), {});
  expect(!unresolved && unresolved.error().message.find("cannot be resolved exactly") != std::string::npos &&
             unresolved.error().message.find("restart ava --acp") != std::string::npos && !invalid_service.initialized(),
         "ACP initialize fails actionably when the startup provider/model cannot resolve");

  ava::tests::write_app_test_file(
      invalid_paths.models_file,
      R"({"default_provider":"synthetic","default_model":"declared","models":[{"provider":"synthetic","id":"declared","name":"Declared","family":"fake","supports_tools":false,"supports_streaming":false,"input_modalities":["text"],"output_modalities":["text"]}]})");
  AgentService unknown_provider_service(invalid_options);
  auto unknown_provider = unknown_provider_service.handle_request(initialize_request(), {});
  expect(!unknown_provider && unknown_provider.error().message.find("startup provider is not registered") != std::string::npos &&
             unknown_provider.error().message.find("provider: synthetic") != std::string::npos && !unknown_provider_service.initialized(),
         "ACP rejects a declared startup model whose provider is unavailable");

  std::string synthetic_body;
  auto synthetic_options = invalid_options;
  synthetic_options.provider_bundle_factory = recording_bundle_factory(&synthetic_body);
  AgentService synthetic_provider_service(std::move(synthetic_options));
  auto synthetic_initialized = synthetic_provider_service.handle_request(initialize_request(), {});
  expect(synthetic_initialized && synthetic_provider_service.initialized(),
         "ACP custom provider bundle factories may supply synthetic declared providers for tests and embeddings");

  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  std::filesystem::remove_all(invalid_root, cleanup);
}

void test_acp_resume_validates_history_against_pinned_startup_model()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-resume-model-history");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const paths = ava::tests::app_test_paths(root);
  std::filesystem::create_directories(paths.ava_config_dir);
  ava::tests::write_app_test_file(
      paths.models_file,
      R"({"default_provider":"moonshot","default_model":"image-model","models":[{"provider":"moonshot","id":"image-model","name":"Image","family":"fake","supports_tools":false,"supports_streaming":false,"input_modalities":["text","image"],"output_modalities":["text"]}]})");

  std::string image_body;
  AgentServiceOptions image_options;
  image_options.agent_version = "1";
  image_options.launch_root = std::filesystem::canonical(workspace);
  image_options.paths = paths;
  image_options.provider_bundle_factory = recording_bundle_factory(&image_body);
  AgentService image_service(image_options);
  image_service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  static_cast<void>(image_service.handle_request(initialize_request(), {}));
  auto created = image_service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  RequestResult prompted;
  if (session_id)
  {
    prompted =
        image_service.handle_request(Request{.id = std::int64_t(3),
                                             .method = "session/prompt",
                                             .params_json = std::string("{\"sessionId\":\"") + *session_id +
                                                            "\",\"prompt\":[{\"type\":\"image\",\"data\":\"iVBORw0KGgo=\",\"mimeType\":\"image/png\"}]}"},
                                     {});
    static_cast<void>(image_service.handle_request(
        Request{.id = std::int64_t(4), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *session_id + "\"}"}, {}));
  }
  image_service.shutdown();
  expect(session_id && prompted, "ACP image-capable fixture persists compatible image history before restart");

  ava::tests::write_app_test_file(
      paths.models_file,
      R"({"default_provider":"moonshot","default_model":"text-model","models":[{"provider":"moonshot","id":"text-model","name":"Text","family":"fake","supports_tools":false,"supports_streaming":false,"input_modalities":["text"],"output_modalities":["text"]}]})");
  std::string text_body;
  AgentServiceOptions text_options = image_options;
  text_options.provider_bundle_factory = recording_bundle_factory(&text_body);
  AgentService text_service(std::move(text_options));
  static_cast<void>(text_service.handle_request(initialize_request(), {}));
  RequestResult resumed;
  if (session_id)
    resumed = text_service.handle_request(
        Request{.id = std::int64_t(5),
                .method = "session/resume",
                .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"cwd\":\"" + workspace.string() + "\",\"mcpServers\":[]}"},
        {});
  expect(session_id && !resumed && resumed.error().code == -32602 && resumed.error().message.find("image input support") != std::string::npos,
         "ACP resume rejects image history that the immutable text-only startup model cannot replay");
  text_service.shutdown();

  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_session_lifecycle_real_prompt_and_provider_ownership()
{
  using namespace ava::app::acp;
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-m3-test");
  auto workspace = root / "workspace";
  auto nested = workspace / "nested";
  std::filesystem::create_directories(nested);
  auto paths = ava::tests::app_test_paths(root);
  std::filesystem::create_directories(paths.ava_config_dir);
  ava::tests::write_app_test_file(
      paths.models_file,
      R"({"default_provider":"moonshot","default_model":"acp-test","models":[{"provider":"moonshot","id":"acp-test","name":"ACP Test","family":"fake","supports_tools":false,"supports_streaming":false,"input_modalities":["text"],"output_modalities":["text"]}]})");

  std::mutex ownership_mutex;
  std::set<void const*> transport_instances;
  std::size_t bundle_count = 0;
  ava::app::RuntimeProviderRunBundleFactory factory = [&](ava::app::runtime::Session const&, ava::app::runtime::RunOptions options,
                                                          std::string_view) -> ava::core::Result<ava::app::RuntimeProviderRunBundle> {
    auto transport = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{ava::provider::HttpResponse{
        .status_code = 200, .headers = {}, .body = R"({"choices":[{"message":{"content":"owned response"},"finish_reason":"stop"}]})"}});
    {
      std::lock_guard lock(ownership_mutex);
      ++bundle_count;
      transport_instances.insert(transport.get());
    }
    options.access_token = "fake-test-key";
    options.stream = false;
    std::unique_ptr<ava::provider::Transport> auth_transport = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
    std::unique_ptr<ava::provider::Transport> run_transport = std::move(transport);
    auto created_provider = ava::provider::builtin_provider_registry().create("moonshot");
    if (!created_provider)
      return std::unexpected(std::move(created_provider.error()));
    std::unique_ptr<ava::provider::Provider> provider = std::move(*created_provider);
    return ava::app::RuntimeProviderRunBundle{
        .provider = std::move(provider), .transport = std::move(run_transport), .auth_transport = std::move(auth_transport), .options = std::move(options)};
  };

  AgentServiceOptions options;
  options.agent_version = "1.0.0";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = std::move(factory);
  AgentService service(std::move(options));
  std::vector<std::string> updates;
  service.bind_update_sender([&](std::string_view session_id, std::string_view update) -> ava::core::VoidResult {
    updates.push_back(std::string(session_id) + ":" + std::string(update));
    return {};
  });

  auto initialized = service.handle_request(Request{.id = std::int64_t(1), .method = "initialize", .params_json = std::string(R"({"protocolVersion":1})")}, {});
  expect(initialized && initialized->find(R"("image":false)") != std::string::npos && initialized->find(R"("loadSession":false)") != std::string::npos,
         "ACP M4 test service derives text-only startup capabilities from its effective default model");
  auto first = service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto second = service.handle_request(
      Request{.id = std::int64_t(3), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + nested.string() + "\",\"mcpServers\":[]}"}, {});
  auto first_id = first ? ava::core::json::string_field(*first, "sessionId") : std::nullopt;
  auto second_id = second ? ava::core::json::string_field(*second, "sessionId") : std::nullopt;
  expect(first_id && second_id && *first_id != *second_id, "ACP connection creates independent session ids");
  if (first_id && second_id)
  {
    auto image_prompt =
        service.handle_request(Request{.id = std::int64_t(30),
                                       .method = "session/prompt",
                                       .params_json = std::string("{\"sessionId\":\"") + *first_id +
                                                      "\",\"prompt\":[{\"type\":\"image\",\"data\":\"iVBORw0KGgo=\",\"mimeType\":\"image/png\"}]}"},
                               {});
    auto image_store = ava::session::SessionStore::open(workspace, *first_id, paths.sessions_dir);
    bool const attachment_storage_absent = image_store && !std::filesystem::exists(ava::session::attachment_storage_root(*image_store));
    expect(!image_prompt && image_prompt.error().code == -32602 && bundle_count == 0 && attachment_storage_absent,
           "ACP rejects image content for a text-only session before provider setup or attachment import");

    auto prompt_params = [](std::string const& id, std::string_view text) {
      return std::string("{\"sessionId\":\"") + id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"" + std::string(text) + "\"}]}";
    };
    auto first_prompt = service.handle_request(Request{.id = std::int64_t(4), .method = "session/prompt", .params_json = prompt_params(*first_id, "one")}, {});
    auto second_prompt =
        service.handle_request(Request{.id = std::int64_t(5), .method = "session/prompt", .params_json = prompt_params(*second_id, "two")}, {});
    auto prompt_detail = first_prompt ? *first_prompt : first_prompt.error().message;
    prompt_detail += " / ";
    prompt_detail += (second_prompt ? *second_prompt : second_prompt.error().message);
    expect(first_prompt && second_prompt && *first_prompt == R"({"stopReason":"end_turn"})" && *second_prompt == R"({"stopReason":"end_turn"})",
           "ACP text prompts execute through the real runtime backend: " + prompt_detail);
  }
  expect(bundle_count == 2 && transport_instances.size() == 2, "each ACP active run owns a distinct provider transport bundle");
  expect(updates.size() == 2 && updates[0].find("owned response") != std::string::npos && updates[1].find("owned response") != std::string::npos,
         "ACP emits one final text update per non-streaming prompt without duplication");

  service.shutdown();
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
}

void test_acp_exact_identity_persisted_cwd_and_restart()
{
  using namespace ava::app::acp;
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-identity");
  auto workspace = root / "workspace";
  auto nested = workspace / "nested";
  std::filesystem::create_directories(nested);
  configure_acp_test_model(root);
  auto paths = ava::tests::app_test_paths(root);

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  AgentService first(options);
  expect(first.handle_request(initialize_request(), {}).has_value(), "ACP cwd test initializes first host");
  auto created = first.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + nested.string() + "\",\"mcpServers\":[]}"}, {});
  auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  expect(id.has_value(), "ACP cwd test creates a nested-cwd session");
  if (!id)
    return;
  auto listed = first.handle_request(Request{.id = std::int64_t(3), .method = "session/list", .params_json = std::string("{}")}, {});
  expect(listed && listed->find(nested.string()) != std::string::npos, "session/list reports persisted original cwd rather than launch root");
  expect(first.handle_request(Request{.id = std::int64_t(4), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *id + "\"}"}, {})
             .has_value(),
         "ACP cwd test closes first host");
  first.shutdown();

  auto store = ava::session::SessionStore::open(options.launch_root, *id, paths.sessions_dir);
  auto summary = store ? store->inspect_bounded(kAcpSessionReadLimits) : ava::core::Result<ava::session::SessionSummary>(std::unexpected(store.error()));
  expect(summary && summary->original_cwd == nested, "canonical original cwd persists in protocol-neutral session metadata");

  AgentService second(options);
  static_cast<void>(second.handle_request(initialize_request(), {}));
  auto prefix = id->substr(0, id->size() - 2);
  auto load = second.handle_request(Request{.id = std::int64_t(5), .method = "session/load", .params_json = std::string("{}")}, {});
  expect(!load && load.error().code == -32601, "ACP rejects unadvertised session/load rather than returning partial rich history");
  auto prefix_resume = second.handle_request(Request{.id = std::int64_t(50),
                                                     .method = "session/resume",
                                                     .params_json = std::string("{\"sessionId\":\"") + prefix + "\",\"cwd\":\"" + nested.string() + "\"}"},
                                             {});
  expect(!prefix_resume && prefix_resume.error().code == -32002, "ACP exact resume lookup rejects a unique CLI-style id prefix without scanning");
  auto mismatch = second.handle_request(Request{.id = std::int64_t(6),
                                                .method = "session/resume",
                                                .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"cwd\":\"" + workspace.string() + "\"}"},
                                        {});
  expect(!mismatch && mismatch.error().code == -32602, "ACP load/resume rejects client-selected cwd drift after restart");
  auto resumed = second.handle_request(
      Request{
          .id = std::int64_t(7), .method = "session/resume", .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"cwd\":\"" + nested.string() + "\"}"},
      {});
  expect(resumed.has_value(), "ACP resumes exact id when requested cwd matches persisted cwd");
  second.shutdown();

  auto const oversized_path = store->session_path();
  std::string const oversized_bytes(kAcpSessionReadLimits.max_file_bytes + 1, 'x');
  {
    std::ofstream file(oversized_path, std::ios::binary | std::ios::trunc);
    file.write(oversized_bytes.data(), static_cast<std::streamsize>(oversized_bytes.size()));
  }
  AgentService bounded(options);
  static_cast<void>(bounded.handle_request(initialize_request(), {}));
  auto oversized_resume = bounded.handle_request(
      Request{
          .id = std::int64_t(8), .method = "session/resume", .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"cwd\":\"" + nested.string() + "\"}"},
      {});
  bounded.shutdown();
  std::ifstream oversized_file(oversized_path, std::ios::binary);
  std::string oversized_after((std::istreambuf_iterator<char>(oversized_file)), std::istreambuf_iterator<char>());
  bool quarantine_exists = false;
  auto const quarantine_prefix = oversized_path.filename().string() + ".torn-tail.";
  std::error_code quarantine_iter_error;
  for (std::filesystem::directory_iterator iterator(oversized_path.parent_path(), quarantine_iter_error), end; !quarantine_iter_error && iterator != end;
       iterator.increment(quarantine_iter_error))
  {
    quarantine_exists = quarantine_exists || iterator->path().filename().string().starts_with(quarantine_prefix);
  }
  expect(!oversized_resume && oversized_after == oversized_bytes && !quarantine_exists,
         "ACP session/resume passes bounded recovery limits and rejects an oversized file unchanged without quarantine");

  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_cross_process_lease_and_bounded_streaming()
{
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-lease");
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto store = ava::session::SessionStore::create(workspace, root / "sessions");
  expect(store.has_value(), "lease test creates store");
  if (!store)
    return;
  static_cast<void>(append_session_entry_for_test(*store, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                             .parent_id = "",
                                                             .type = ava::session::EntryType::SessionStart,
                                                             .timestamp = ava::session::now_timestamp(),
                                                             .data_json = "{}"}));
  auto lease = ava::session::SessionLease::acquire(store->session_path());
  auto same_process = ava::session::SessionLease::acquire(store->session_path());
  expect(lease && !same_process && same_process.error().message().find("already owned") != std::string::npos,
         "session lease excludes a second owner in the same process with an actionable error");
  pid_t child = fork();
  if (child == 0)
  {
    auto contested = ava::session::SessionLease::acquire(store->session_path());
    _exit(!contested && contested.error().message().find("already owned") != std::string::npos ? 0 : 1);
  }
  int status = 0;
  static_cast<void>(waitpid(child, &status, 0));
  expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "session lease excludes a second AVA process for the host lifetime");

  if (lease)
  {
    static_cast<void>(store->append(*lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                       .parent_id = "",
                                                                       .type = ava::session::EntryType::UserMessage,
                                                                       .timestamp = ava::session::now_timestamp(),
                                                                       .data_json = "{\"text\":\"0123456789abcdef\"}"}));
  }
  auto bounded = store->load_bounded(ava::session::SessionReadLimits{.max_file_bytes = 32, .max_line_bytes = 32, .max_entries = 2});
  expect(!bounded && bounded.error().message().find("bounded") != std::string::npos,
         "bounded streaming session open rejects an oversized transcript without unbounded allocation");
  auto recovered = store->load_bounded(ava::session::SessionReadLimits{.max_file_bytes = 4096, .max_line_bytes = 2048, .max_entries = 8});
  expect(recovered && recovered->size() == 2, "bounded session reader recovers on a later request with valid budgets");
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_list_pagination_cancel_race_stop_reasons_and_file_safety()
{
  using namespace ava::app::acp;
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-pagination");
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);
  auto paths = ava::tests::app_test_paths(root);
  for (int index = 0; index < 55; ++index)
  {
    auto store = ava::session::SessionStore::create(workspace, paths.sessions_dir);
    if (!store)
      continue;
    static_cast<void>(append_session_entry_for_test(*store, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                               .parent_id = "",
                                                               .type = ava::session::EntryType::SessionStart,
                                                               .timestamp = "2026-07-12T12:" + std::to_string(index / 10) + std::to_string(index % 10) + ":00Z",
                                                               .data_json = "{\"original_cwd\":\"" + ava::core::json::escape(workspace.string()) + "\"}"}));
  }
  std::string request_body;
  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = recording_bundle_factory(&request_body);
  AgentService service(options);
  service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  static_cast<void>(service.handle_request(initialize_request(), {}));
  auto first_page = service.handle_request(Request{.id = std::int64_t(2), .method = "session/list", .params_json = std::string("{}")}, {});
  auto const first_items = first_page ? ava::core::json::objects_in_array_field(*first_page, "sessions") : std::vector<std::string>{};
  auto cursor = first_page ? ava::core::json::string_field(*first_page, "nextCursor").value_or("") : "";
  expect(first_page && first_items.size() == kSessionListPageSize && !cursor.empty() && first_page->size() < kMaxRecordBytes,
         "session/list returns a bounded first page and pinned-schema nextCursor");
  auto second_page =
      service.handle_request(Request{.id = std::int64_t(3), .method = "session/list", .params_json = std::string("{\"cursor\":\"") + cursor + "\"}"}, {});
  auto const second_items = second_page ? ava::core::json::objects_in_array_field(*second_page, "sessions") : std::vector<std::string>{};
  expect(second_page && second_items.size() == 5 && !ava::core::json::string_field(*second_page, "nextCursor"),
         "session/list cursor yields a stable bounded remainder page");
  auto invalid_cursor =
      service.handle_request(Request{.id = std::int64_t(4), .method = "session/list", .params_json = std::string(R"({"cursor":"v2:forged"})")}, {});
  expect(!invalid_cursor && invalid_cursor.error().code == -32602, "session/list rejects forged or unsupported cursor state");

  for (int index = 0; index < 2000; ++index)
  {
    auto store = ava::session::SessionStore::create(workspace, paths.sessions_dir);
    if (!store)
      continue;
    static_cast<void>(append_session_entry_for_test(*store, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                               .parent_id = "",
                                                               .type = ava::session::EntryType::SessionStart,
                                                               .timestamp = ava::session::now_timestamp(),
                                                               .data_json = "{\"original_cwd\":\"" + ava::core::json::escape(workspace.string()) + "\"}"}));
    static_cast<void>(append_session_metadata_for_test(
        *store, ava::session::SessionMetadataUpdate{.name = std::optional<std::string>(std::string(256, 't')), .actor = "test"}));
  }
  std::vector<std::string> retained_cursors;
  for (int index = 0; index < 12; ++index)
  {
    auto listed = service.handle_request(Request{.id = std::int64_t(100 + index), .method = "session/list", .params_json = std::string("{}")}, {});
    auto token = listed ? ava::core::json::string_field(*listed, "nextCursor") : std::nullopt;
    if (token)
      retained_cursors.push_back(*token);
  }
  auto evicted =
      retained_cursors.empty()
          ? RequestResult(std::unexpected(JsonRpcError{}))
          : service.handle_request(
                Request{.id = std::int64_t(200), .method = "session/list", .params_json = std::string("{\"cursor\":\"") + retained_cursors.front() + "\"}"},
                {});
  auto newest =
      retained_cursors.empty()
          ? RequestResult(std::unexpected(JsonRpcError{}))
          : service.handle_request(
                Request{.id = std::int64_t(201), .method = "session/list", .params_json = std::string("{\"cursor\":\"") + retained_cursors.back() + "\"}"}, {});
  expect(retained_cursors.size() == 12 && !evicted && evicted.error().code == -32602 && newest,
         "session/list enforces aggregate snapshot bytes with deterministic oldest-first eviction and invalidates evicted cursors: cursors=" +
             std::to_string(retained_cursors.size()) + " evicted=" + (evicted ? std::string("success") : std::to_string(evicted.error().code)) +
             " newest=" + (newest ? std::string("success") : std::to_string(newest.error().code)));

  auto secret = workspace / "secret.txt";
  ava::tests::write_app_test_file(secret, "MUST_NOT_ENTER_PROVIDER_CONTEXT");
  auto created = service.handle_request(
      Request{.id = std::int64_t(5), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  if (id)
  {
    service.handle_notification(Notification{.method = "session/cancel", .params_json = std::string("{\"sessionId\":\"") + *id + "\"}"}, {});
    auto canceled = service.handle_request(
        Request{.id = std::int64_t(6),
                .method = "session/prompt",
                .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"queued cancel\"}]}"},
        {});
    expect(canceled && *canceled == R"({"stopReason":"end_turn"})" && !request_body.empty(),
           std::string("idle session/cancel is a no-op and cannot cancel a future prompt: ") + (canceled ? *canceled : canceled.error().message) +
               " body=" + request_body.substr(0, 128));
    auto prompt =
        service.handle_request(Request{.id = std::int64_t(7),
                                       .method = "session/prompt",
                                       .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"@secret.txt\"}]}"},
                               {});
    auto detail = std::string("ACP prompt preserves @file as literal text and performs no implicit local file read: ") +
                  (prompt ? *prompt : prompt.error().message) + " body=" + request_body.substr(0, 512);
    expect(request_body.find("@secret.txt") != std::string::npos && request_body.find("MUST_NOT_ENTER_PROVIDER_CONTEXT") == std::string::npos, detail);
  }
  bool outcomes_exhaustive = true;
  for (auto const& entry : ava::core::kRuntimeTerminalOutcomeCatalog)
  {
    auto mapped = acp_stop_reason(entry.outcome);
    std::string_view expected;
    switch (entry.outcome)
    {
      case ava::core::RuntimeTerminalOutcome::Completed:
        expected = "end_turn";
        break;
      case ava::core::RuntimeTerminalOutcome::MaxTokens:
        expected = "max_tokens";
        break;
      case ava::core::RuntimeTerminalOutcome::MaxTurnRequests:
        expected = "max_turn_requests";
        break;
      case ava::core::RuntimeTerminalOutcome::Refusal:
        expected = "refusal";
        break;
      case ava::core::RuntimeTerminalOutcome::Cancelled:
        expected = "cancelled";
        break;
      case ava::core::RuntimeTerminalOutcome::Error:
        break;
    }
    outcomes_exhaustive = outcomes_exhaustive && (entry.outcome == ava::core::RuntimeTerminalOutcome::Error ? !mapped : mapped && *mapped == expected);
  }
  expect(outcomes_exhaustive, "ACP exhaustively maps the closed protocol-neutral runtime outcome catalog");
  service.shutdown();
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_cancel_terminal_arbitration_and_provider_setup_paths()
{
  using namespace ava::app::acp;

  auto run_phase_case = [](ava::app::RunPhase phase, bool expect_cancel) {
    auto root = std::filesystem::temp_directory_path() / ava::core::make_id(expect_cancel ? "acp-cancel-before" : "acp-cancel-late");
    auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    configure_acp_test_model(root);
    auto paths = ava::tests::app_test_paths(root);
    auto transport_state = std::make_shared<CapturingSequenceState>();
    auto barrier = std::make_shared<RunPhaseBarrier>();
    barrier->target = phase;

    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = paths;
    options.run_options.on_phase = [barrier](ava::app::RunPhase observed) { return barrier->observe(observed); };
    options.provider_bundle_factory = sequence_bundle_factory(transport_state, {acp_text_response("terminal success")});
    AgentService service(options);
    std::mutex updates_mutex;
    std::vector<std::string> updates;
    service.bind_update_sender([&](std::string_view, std::string_view update) -> ava::core::VoidResult {
      std::lock_guard lock(updates_mutex);
      updates.emplace_back(update);
      return {};
    });
    static_cast<void>(service.handle_request(initialize_request(), {}));
    auto created = service.handle_request(
        Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
    auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
    expect(id.has_value(), "ACP terminal arbitration fixture creates a session");
    if (!id)
      return;

    RequestResult prompt_result;
    std::jthread prompt_thread([&] {
      prompt_result =
          service.handle_request(Request{.id = std::int64_t(3),
                                         .method = "session/prompt",
                                         .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"race\"}]}"},
                                 {});
    });
    bool const reached = barrier->wait_until_reached();
    expect(reached,
           expect_cancel ? "ACP prompt reaches deterministic pre-Completing barrier" : "ACP prompt reaches deterministic committed Completing barrier");
    if (reached)
      service.handle_notification(Notification{.method = "session/cancel", .params_json = std::string("{\"sessionId\":\"") + *id + "\"}"}, {});
    barrier->release();
    prompt_thread.join();
    service.shutdown();

    std::size_t request_count = 0;
    {
      std::lock_guard lock(transport_state->mutex);
      request_count = transport_state->request_bodies.size();
    }
    std::size_t update_count = 0;
    {
      std::lock_guard lock(updates_mutex);
      update_count = updates.size();
    }
    auto store = ava::session::SessionStore::open(workspace, *id, paths.sessions_dir);
    auto entries = store ? store->load()
                         : ava::core::Result<std::vector<ava::session::SessionEntry>>(
                               std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "store unavailable")));
    std::size_t assistant_entries = 0;
    if (entries)
      assistant_entries = static_cast<std::size_t>(
          std::count_if(entries->begin(), entries->end(), [](auto const& entry) { return entry.type == ava::session::EntryType::AssistantMessage; }));

    if (expect_cancel)
    {
      expect(prompt_result && *prompt_result == R"({"stopReason":"cancelled"})" && request_count == 0 && update_count == 0 && assistant_entries == 0,
             "cancel accepted before Completing yields only a canceled terminal with no provider output or durable assistant");
    }
    else
    {
      expect(prompt_result && *prompt_result == R"({"stopReason":"end_turn"})" && request_count == 1 && update_count == 1 && assistant_entries == 1,
             "cancel after Completing is a no-op and preserves emitted and durable terminal success");
    }
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
  };

  run_phase_case(ava::app::RunPhase::AwaitingProvider, true);
  run_phase_case(ava::app::RunPhase::Completing, false);

  auto run_setup_case = [](bool fail_setup, bool cancel_setup, ava::core::ErrorCategory failure_category = ava::core::ErrorCategory::Provider) {
    auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-provider-setup-cancel");
    auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    configure_acp_test_model(root);
    auto paths = ava::tests::app_test_paths(root);
    struct SetupGate
    {
      std::mutex mutex;
      std::condition_variable cv;
      bool entered = false;
      bool released = false;
    };
    auto gate = std::make_shared<SetupGate>();
    auto transport_state = std::make_shared<CapturingSequenceState>();

    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = paths;
    options.provider_bundle_factory = [gate, transport_state, fail_setup, failure_category](
                                          ava::app::runtime::Session const&, ava::app::runtime::RunOptions run_options,
                                          std::string_view) -> ava::core::Result<ava::app::RuntimeProviderRunBundle> {
      {
        std::unique_lock lock(gate->mutex);
        gate->entered = true;
        gate->cv.notify_all();
        gate->cv.wait(lock, [&] { return gate->released; });
      }
      if (fail_setup)
        return std::unexpected(ava::core::Error(failure_category, "provider setup failed deterministically"));
      auto provider = ava::provider::builtin_provider_registry().create("moonshot");
      if (!provider)
        return std::unexpected(std::move(provider.error()));
      run_options.access_token = "test";
      run_options.stream = false;
      std::unique_ptr<ava::provider::Transport> transport =
          std::make_unique<CapturingSequenceTransport>(transport_state, std::vector<ava::provider::HttpResponse>{acp_text_response()});
      std::unique_ptr<ava::provider::Transport> auth = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
      return ava::app::RuntimeProviderRunBundle{
          .provider = std::move(*provider), .transport = std::move(transport), .auth_transport = std::move(auth), .options = std::move(run_options)};
    };
    AgentService service(options);
    service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
    static_cast<void>(service.handle_request(initialize_request(), {}));
    auto created = service.handle_request(
        Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
    auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
    expect(id.has_value(), "ACP provider-setup arbitration fixture creates a session");
    if (!id)
      return;

    RequestResult prompt_result;
    std::jthread prompt_thread([&] {
      prompt_result =
          service.handle_request(Request{.id = std::int64_t(3),
                                         .method = "session/prompt",
                                         .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"setup\"}]}"},
                                 {});
    });
    {
      std::unique_lock lock(gate->mutex);
      static_cast<void>(gate->cv.wait_for(lock, 2s, [&] { return gate->entered; }));
    }
    if (cancel_setup)
      service.handle_notification(Notification{.method = "session/cancel", .params_json = std::string("{\"sessionId\":\"") + *id + "\"}"}, {});
    {
      std::lock_guard lock(gate->mutex);
      gate->released = true;
    }
    gate->cv.notify_all();
    prompt_thread.join();
    service.shutdown();

    if (cancel_setup)
      expect(prompt_result && *prompt_result == R"({"stopReason":"cancelled"})", fail_setup
                                                                                     ? "cancel accepted during failing provider setup wins the setup error"
                                                                                     : "cancel accepted during provider setup stops the admitted prompt");
    else
      expect(!prompt_result && prompt_result.error().code == -32603 &&
                 prompt_result.error().message.find("provider setup failed deterministically") != std::string::npos,
             "non-authentication provider and permission setup errors remain ACP internal errors when cancellation did not win");
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
  };

  run_setup_case(false, true);
  run_setup_case(true, true);
  run_setup_case(true, false);
  run_setup_case(true, false, ava::core::ErrorCategory::PermissionDenied);
}

void test_acp_session_mcp_requires_persistent_operator_authorization()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-mcp-operator-auth");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_tool_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);
  auto const marker = root / "mcp-must-not-start";
  auto provider_state = std::make_shared<CapturingSequenceState>();

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = sequence_bundle_factory(provider_state, {acp_text_response("must not run")});
  AgentService service(options);
  service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  std::atomic_int permission_requests = 0;
  service.bind_client_request_sender(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "session/request_permission")
          ++permission_requests;
        std::promise<CallResult> promise;
        promise.set_value(std::string(R"({"outcome":{"outcome":"selected","optionId":"allow_always"}})"));
        return PendingCall{.id = "untrusted-mcp-allow", .completion = promise.get_future()};
      },
      [](JsonRpcId const&, std::string) { return true; });
  static_cast<void>(service.handle_request(initialize_request(), {}));

  auto const server_json =
      std::string("{\"name\":\"touch\",\"command\":\"/usr/bin/touch\",\"args\":[\"") + ava::core::json::escape(marker.string()) + "\"],\"env\":[]}";
  auto created = service.handle_request(Request{.id = std::int64_t(2),
                                                .method = "session/new",
                                                .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[" + server_json + "]}"},
                                        {});
  auto const id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  RequestResult prompted;
  if (id)
    prompted = service.handle_request(
        Request{.id = std::int64_t(3),
                .method = "session/prompt",
                .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"discover untrusted MCP\"}]}"},
        {});
  service.shutdown();

  std::size_t provider_requests = 0;
  {
    std::lock_guard lock(provider_state->mutex);
    provider_requests = provider_state->request_bodies.size();
  }
  auto const detail =
      std::string("ACP session MCP requires protected persistent operator authorization: result=") + (prompted ? *prompted : prompted.error().message);
  expect(id && !prompted && prompted.error().message.find("persistent operator authorization is required") != std::string::npos &&
             permission_requests.load() == 0 && provider_requests == 0 && !std::filesystem::exists(marker),
         detail);

  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_strict_session_mcp_registry_and_error_propagation()
{
  using namespace ava::app::acp;
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-strict-mcp");
  auto workspace = root / "workspace";
  auto nested = workspace / "nested";
  std::filesystem::create_directories(nested);
  configure_acp_tool_test_model(root);
  auto paths = ava::tests::app_test_paths(root);
  auto const cwd_marker = root / "mcp-cwd.txt";
  auto transport_state = std::make_shared<CapturingSequenceState>();
  std::vector<ava::provider::HttpResponse> responses{
      ava::provider::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body =
              R"({"choices":[{"message":{"tool_calls":[{"id":"call_mcp","function":{"name":"mcp_demo_echo","arguments":"{\"text\":\"hello\"}"}}]},"finish_reason":"tool_calls"}]})"},
      acp_text_response("strict MCP complete")};

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = sequence_bundle_factory(transport_state, std::move(responses));
  AgentService service(options);
  service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  static_cast<void>(service.handle_request(initialize_request(), {}));
  auto const server_json = std::string("{\"name\":\"demo\",\"command\":\"") + ava::core::json::escape(AVA_FAKE_MCP_SERVER_PATH) +
                           "\",\"args\":[\"cwd-marker\",\"" + ava::core::json::escape(cwd_marker.string()) + "\"],\"env\":[]}";
  ava::permissions::PermissionRuleStore const rule_store{.global_rules_file = paths.ava_config_dir / "permission-rules.json",
                                                         .workspace_rules_file = workspace / ".ava" / "permission-rules.json",
                                                         .workspace_dir = workspace};
  auto install_mcp_allow = [&](ava::permissions::Operation operation, std::string command, std::string tool_name) {
    auto added = ava::permissions::add_persistent_permission_rule(
        rule_store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                          .action = ava::permissions::PermissionAction::Allow,
                                                          .operation = operation,
                                                          .mode = ava::permissions::PermissionRuleMode::Build,
                                                          .tool_name = std::move(tool_name),
                                                          .target_path = {},
                                                          .command = std::move(command),
                                                          .reason = "authorize exact ACP session MCP operation",
                                                          .actor = "test_operator"});
    expect(added.has_value(), added ? "protected persistent ACP MCP rule installed" : "protected persistent ACP MCP rule installed: " + added.error().format());
    return added.has_value();
  };
  ava::mcp::McpServerConfig const demo_server{.id = "demo",
                                              .name = "demo",
                                              .command = AVA_FAKE_MCP_SERVER_PATH,
                                              .args = {"cwd-marker", cwd_marker.string()},
                                              .env = {},
                                              .enabled = true,
                                              .scope = ava::mcp::McpServerScope::Project,
                                              .source_path = {}};
  auto const demo_launch_identity = ava::mcp::session_mcp_launch_identity(demo_server, std::filesystem::canonical(nested));
  auto const demo_tool_name = ava::mcp::mcp_model_tool_name("demo", "echo");
  bool valid_rules = install_mcp_allow(ava::permissions::Operation::McpServerLaunch, demo_launch_identity, "mcp_discovery");
  valid_rules = install_mcp_allow(ava::permissions::Operation::McpServerConnect, "demo", "mcp_discovery") && valid_rules;
  valid_rules = install_mcp_allow(ava::permissions::Operation::McpServerLaunch, demo_launch_identity, demo_tool_name) && valid_rules;
  valid_rules = install_mcp_allow(ava::permissions::Operation::McpServerConnect, "demo", demo_tool_name) && valid_rules;
  valid_rules = install_mcp_allow(ava::permissions::Operation::McpToolCall, "demo:echo", demo_tool_name) && valid_rules;
  expect(valid_rules, "ACP strict MCP fixture has exact protected launch, connect, and tool-call operator Allows");

  auto created = service.handle_request(Request{.id = std::int64_t(2),
                                                .method = "session/new",
                                                .params_json = std::string("{\"cwd\":\"") + nested.string() + "\",\"mcpServers\":[" + server_json + "]}"},
                                        {});
  auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  expect(id.has_value(), "ACP strict MCP fixture creates a nested-cwd session");
  if (id)
  {
    auto prompted =
        service.handle_request(Request{.id = std::int64_t(3),
                                       .method = "session/prompt",
                                       .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"use MCP\"}]}"},
                               {});
    std::vector<std::string> bodies;
    {
      std::lock_guard lock(transport_state->mutex);
      bodies = transport_state->request_bodies;
    }
    auto const valid_detail = std::string("ACP registry composes approved built-ins and valid session MCP from persisted cwd: result=") +
                              (prompted ? *prompted : prompted.error().message) + " bodies=" + std::to_string(bodies.size()) +
                              " marker=" + read_acp_test_file(cwd_marker) + (bodies.empty() ? std::string{} : " first=" + bodies.front().substr(0, 256));
    expect(prompted && *prompted == R"({"stopReason":"end_turn"})" && bodies.size() == 2 &&
               bodies.front().find("\"name\":\"mcp_demo_echo\"") != std::string::npos && bodies.front().find("\"name\":\"read_file\"") != std::string::npos &&
               bodies.front().find("\"name\":\"question\"") == std::string::npos && bodies.front().find("\"name\":\"task\"") == std::string::npos &&
               bodies.front().find("\"name\":\"bash\"") == std::string::npos && bodies.front().find("\"name\":\"webfetch\"") == std::string::npos &&
               bodies.front().find("\"name\":\"lsp_") == std::string::npos && bodies.front().find("\"name\":\"plugin_") == std::string::npos &&
               bodies.back().find("MCP call ok") != std::string::npos && read_acp_test_file(cwd_marker) == std::filesystem::canonical(nested).string(),
           valid_detail);
  }
  service.shutdown();

  auto const bash_marker = nested / "bash-must-not-run";
  auto bash_state = std::make_shared<CapturingSequenceState>();
  auto const bash_arguments = std::string("{\"command\":\"printf exposed > ") + ava::core::json::escape(bash_marker.string()) + "\"}";
  std::vector<ava::provider::HttpResponse> bash_responses{
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = std::string(R"({"choices":[{"message":{"tool_calls":[{"id":"call_bash","function":{"name":"bash","arguments":")") +
                                          ava::core::json::escape(bash_arguments) + R"("}}]},"finish_reason":"tool_calls"}]})"},
      acp_text_response("bash stayed unavailable")};
  auto bash_options = options;
  bash_options.provider_bundle_factory = sequence_bundle_factory(bash_state, std::move(bash_responses));
  AgentService bash_service(std::move(bash_options));
  bash_service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  static_cast<void>(bash_service.handle_request(initialize_request(), {}));
  auto bash_created = bash_service.handle_request(
      Request{.id = std::int64_t(20), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + nested.string() + "\",\"mcpServers\":[]}"}, {});
  auto bash_id = bash_created ? ava::core::json::string_field(*bash_created, "sessionId") : std::nullopt;
  RequestResult bash_prompt;
  if (bash_id)
    bash_prompt = bash_service.handle_request(
        Request{.id = std::int64_t(21),
                .method = "session/prompt",
                .params_json = std::string("{\"sessionId\":\"") + *bash_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"attempt bash\"}]}"},
        {});
  std::vector<std::string> bash_bodies;
  {
    std::lock_guard lock(bash_state->mutex);
    bash_bodies = bash_state->request_bodies;
  }
  expect(bash_id && bash_prompt && bash_bodies.size() == 2 && bash_bodies.front().find("\"name\":\"bash\"") == std::string::npos &&
             bash_bodies.back().find("unknown tool") != std::string::npos && !std::filesystem::exists(bash_marker),
         "ACP exact M4 registry neither exposes nor dispatches bash when a model attempts an unadvertised call");
  bash_service.shutdown();

  auto run_error_case = [&](std::string mcp_servers, std::string_view expected,
                            std::vector<std::pair<ava::permissions::Operation, std::string>> const& authorizations) {
    bool rules_installed = true;
    for (auto const& [operation, command] : authorizations) rules_installed = install_mcp_allow(operation, command, "mcp_discovery") && rules_installed;
    expect(rules_installed, "ACP strict MCP error fixture has exact protected launch/connect operator Allows");

    auto error_state = std::make_shared<CapturingSequenceState>();
    AgentServiceOptions error_options;
    error_options.agent_version = "1";
    error_options.launch_root = std::filesystem::canonical(workspace);
    error_options.paths = paths;
    error_options.provider_bundle_factory = sequence_bundle_factory(error_state, {acp_text_response()});
    AgentService error_service(error_options);
    error_service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
    static_cast<void>(error_service.handle_request(initialize_request(), {}));
    auto error_created =
        error_service.handle_request(Request{.id = std::int64_t(10),
                                             .method = "session/new",
                                             .params_json = std::string("{\"cwd\":\"") + nested.string() + "\",\"mcpServers\":" + mcp_servers + "}"},
                                     {});
    auto error_id = error_created ? ava::core::json::string_field(*error_created, "sessionId") : std::nullopt;
    RequestResult error_result;
    if (error_id)
      error_result = error_service.handle_request(
          Request{.id = std::int64_t(11),
                  .method = "session/prompt",
                  .params_json = std::string("{\"sessionId\":\"") + *error_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"fail discovery\"}]}"},
          {});
    std::size_t provider_requests = 0;
    {
      std::lock_guard lock(error_state->mutex);
      provider_requests = error_state->request_bodies.size();
    }
    auto const error_detail = std::string("ACP prompt propagates strict MCP registry error: expected=") + std::string(expected) +
                              " actual=" + (error_result ? *error_result : error_result.error().message);
    expect(error_id && !error_result && error_result.error().message.find(expected) != std::string::npos && provider_requests == 0, error_detail);
    error_service.shutdown();
  };

  auto const fake_command = ava::core::json::escape(AVA_FAKE_MCP_SERVER_PATH);
  ava::mcp::McpServerConfig const collision_server{.id = "demo-one",
                                                   .name = "demo-one",
                                                   .command = AVA_FAKE_MCP_SERVER_PATH,
                                                   .args = {},
                                                   .env = {},
                                                   .enabled = true,
                                                   .scope = ava::mcp::McpServerScope::Project,
                                                   .source_path = {}};
  auto const collision_launch_identity = ava::mcp::session_mcp_launch_identity(collision_server, std::filesystem::canonical(nested));
  run_error_case("[{\"name\":\"demo-one\",\"command\":\"" + fake_command + "\",\"args\":[],\"env\":[]},{\"name\":\"demo_one\",\"command\":\"" + fake_command +
                     "\",\"args\":[],\"env\":[]}]",
                 "duplicate model tool name",
                 {{ava::permissions::Operation::McpServerLaunch, collision_launch_identity},
                  {ava::permissions::Operation::McpServerConnect, "demo-one"},
                  {ava::permissions::Operation::McpServerConnect, "demo_one"}});
  auto const missing_command = (root / "missing-server").string();
  auto missing_server = collision_server;
  missing_server.id = "missing";
  missing_server.name = "missing";
  missing_server.command = missing_command;
  auto const missing_launch_identity = ava::mcp::session_mcp_launch_identity(missing_server, std::filesystem::canonical(nested));
  run_error_case("[{\"name\":\"missing\",\"command\":\"" + ava::core::json::escape(missing_command) + "\",\"args\":[],\"env\":[]}]", "mcp_server: missing",
                 {{ava::permissions::Operation::McpServerLaunch, missing_launch_identity}, {ava::permissions::Operation::McpServerConnect, "missing"}});

  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_negotiated_client_filesystem_and_terminal_routing()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-client-tools");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_tool_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);
  auto const note = workspace / "note.txt";
  {
    std::ofstream file(note, std::ios::binary | std::ios::trunc);
    file << "local bytes";
  }

  auto ready = [](std::string id, CallResult result) {
    std::promise<CallResult> promise;
    promise.set_value(std::move(result));
    return PendingCall{.id = std::move(id), .completion = promise.get_future()};
  };
  auto const read_tool_response = ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = std::string(
          R"({"choices":[{"message":{"tool_calls":[{"id":"call_read","function":{"name":"read_file","arguments":"{\"path\":\"note.txt\"}"}}]},"finish_reason":"tool_calls"}]})")};

  auto run_read_case = [&](std::string capabilities, bool expect_remote_read, bool coherent_pair, bool fail_remote = false) {
    {
      std::ofstream file(note, std::ios::binary | std::ios::trunc);
      file << "local bytes";
    }
    auto provider_state = std::make_shared<CapturingSequenceState>();
    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = paths;
    options.provider_bundle_factory = sequence_bundle_factory(provider_state, {read_tool_response, acp_text_response("file complete")});
    AgentService service(options);
    service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
    std::vector<std::string> methods;
    service.bind_client_request_sender(
        [&](std::string method, std::optional<std::string> params, std::chrono::milliseconds, OutboundCallPolicy policy) -> ava::core::Result<PendingCall> {
          methods.push_back(method);
          if (method == "session/request_permission")
            return ready("permission", CallResult(std::string(R"({"outcome":{"outcome":"selected","optionId":"allow_once"}})")));
          expect(method == "fs/read_text_file" && params && params->find(std::filesystem::canonical(note).string()) != std::string::npos &&
                     policy == OutboundCallPolicy::Normal,
                 "negotiated ACP read uses the canonical absolute path DTO with normal delivery policy");
          if (fail_remote)
            return ready("read-failed", std::unexpected(JsonRpcError{.code = -32603,
                                                                     .message = "injected remote read failure",
                                                                     .data_json = std::nullopt,
                                                                     .id = std::nullopt,
                                                                     .intent = EnvelopeIntent::Response,
                                                                     .suppress_response = true}));
          return ready("read", CallResult(std::string(R"({"content":"remote bytes","future":true})")));
        },
        [](JsonRpcId const&, std::string) { return true; });
    auto initialized = service.handle_request(initialize_request_with_capabilities(std::move(capabilities)), {});
    auto created = service.handle_request(
        Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
    auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
    RequestResult prompted;
    if (session_id)
      prompted = service.handle_request(
          Request{.id = std::int64_t(3),
                  .method = "session/prompt",
                  .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"read\"}]}"},
          {});
    std::vector<std::string> bodies;
    {
      std::lock_guard lock(provider_state->mutex);
      bodies = provider_state->request_bodies;
    }
    auto const expected_content = expect_remote_read && !fail_remote ? "remote bytes" : "local bytes";
    auto const visibility_matches = bodies.size() == 2 && (bodies.front().find("\"name\":\"edit_file\"") != std::string::npos) == coherent_pair &&
                                    (bodies.front().find("\"name\":\"apply_patch\"") != std::string::npos) == coherent_pair;
    auto const content_matches = bodies.size() == 2 && (fail_remote ? bodies.back().find("injected remote read failure") != std::string::npos &&
                                                                          bodies.back().find("local bytes") == std::string::npos
                                                                    : bodies.back().find(expected_content) != std::string::npos);
    expect(initialized && session_id && prompted && visibility_matches && content_matches &&
               std::count(methods.begin(), methods.end(), "fs/read_text_file") == (expect_remote_read ? 1 : 0),
           fail_remote ? "a negotiated read failure never falls back to descriptor-secure local bytes"
                       : "ACP routes read_file independently and hides edit/patch only for partial filesystem snapshots");
    service.shutdown();
  };

  run_read_case(R"({"fs":{"readTextFile":true,"writeTextFile":true}})", true, true);
  run_read_case(R"({"fs":{"readTextFile":true,"writeTextFile":false}})", true, false);
  run_read_case(R"({"fs":{"readTextFile":false,"writeTextFile":true}})", false, false);
  run_read_case(R"({"fs":{"readTextFile":false,"writeTextFile":false}})", false, true);
  run_read_case(R"({"fs":{"readTextFile":true,"writeTextFile":false}})", true, false, true);

  auto const write_tool_response = ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = std::string(
          R"({"choices":[{"message":{"tool_calls":[{"id":"call_write","function":{"name":"write_file","arguments":"{\"path\":\"note.txt\",\"content\":\"updated bytes\"}"}}]},"finish_reason":"tool_calls"}]})")};
  auto run_write_case = [&](std::string capabilities, bool expect_remote_write, bool fail_remote = false) {
    {
      std::ofstream file(note, std::ios::binary | std::ios::trunc);
      file << "local bytes";
    }
    auto provider_state = std::make_shared<CapturingSequenceState>();
    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = paths;
    options.provider_bundle_factory = sequence_bundle_factory(provider_state, {write_tool_response, acp_text_response("write complete")});
    AgentService service(options);
    service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
    std::vector<std::string> methods;
    service.bind_client_request_sender(
        [&](std::string method, std::optional<std::string> params, std::chrono::milliseconds, OutboundCallPolicy policy) -> ava::core::Result<PendingCall> {
          methods.push_back(method);
          if (method == "session/request_permission")
            return ready("write-permission", CallResult(std::string(R"({"outcome":{"outcome":"selected","optionId":"allow_once"}})")));
          expect(method == "fs/write_text_file" && params && params->find("updated bytes") != std::string::npos &&
                     policy == OutboundCallPolicy::AbortConnectionIfDelivered,
                 "negotiated ACP write uses the exact client write DTO with fail-stop delivery policy");
          if (fail_remote)
            return ready("write-failed", std::unexpected(JsonRpcError{.code = -32603,
                                                                      .message = "injected remote write failure",
                                                                      .data_json = std::nullopt,
                                                                      .id = std::nullopt,
                                                                      .intent = EnvelopeIntent::Response,
                                                                      .suppress_response = true}));
          return ready("write", CallResult(std::string(R"({"_meta":[]})")));
        },
        [](JsonRpcId const&, std::string) { return true; });
    static_cast<void>(service.handle_request(initialize_request_with_capabilities(std::move(capabilities)), {}));
    auto created = service.handle_request(
        Request{.id = std::int64_t(12), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
    auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
    RequestResult prompted;
    if (session_id)
      prompted = service.handle_request(
          Request{.id = std::int64_t(13),
                  .method = "session/prompt",
                  .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"write\"}]}"},
          {});
    std::string local_content;
    {
      std::ifstream file(note, std::ios::binary);
      local_content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    expect(session_id && prompted && std::count(methods.begin(), methods.end(), "fs/write_text_file") == (expect_remote_write ? 1 : 0) &&
               local_content == (expect_remote_write ? "local bytes" : "updated bytes"),
           fail_remote ? "a negotiated write failure never falls back to a local mutation"
                       : "ACP routes write_file independently while unsupported writes remain descriptor-secure local");
    service.shutdown();
  };

  run_write_case(R"({"fs":{"readTextFile":true,"writeTextFile":false}})", false);
  run_write_case(R"({"fs":{"readTextFile":false,"writeTextFile":true}})", true);
  run_write_case(R"({"fs":{"readTextFile":false,"writeTextFile":true}})", true, true);

  auto terminal_state = std::make_shared<CapturingSequenceState>();
  auto const marker = workspace / "terminal-must-not-run-locally";
  auto const bash_args = std::string("{\"command\":\"touch ") + marker.string() + "\"}";
  auto const bash_tool_response = ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = std::string(R"({"choices":[{"message":{"tool_calls":[{"id":"call_terminal","function":{"name":"bash","arguments":")") +
              ava::core::json::escape(bash_args) + R"("}}]},"finish_reason":"tool_calls"}]})"};
  AgentServiceOptions terminal_options;
  terminal_options.agent_version = "1";
  terminal_options.launch_root = std::filesystem::canonical(workspace);
  terminal_options.paths = paths;
  terminal_options.provider_bundle_factory = sequence_bundle_factory(terminal_state, {bash_tool_response, acp_text_response("terminal complete")});
  AgentService terminal_service(terminal_options);
  terminal_service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  std::vector<std::string> terminal_methods;
  terminal_service.bind_client_request_sender(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        terminal_methods.push_back(method);
        if (method == "session/request_permission")
          return ready("terminal-permission", CallResult(std::string(R"({"outcome":{"outcome":"selected","optionId":"allow_once"}})")));
        if (method == "terminal/create")
          return ready("terminal-create", CallResult(std::string(R"({"terminalId":"negotiated-terminal"})")));
        if (method == "terminal/wait_for_exit")
          return ready("terminal-wait", CallResult(std::string(R"({"exitCode":4294967295,"signal":null})")));
        if (method == "terminal/output")
          return ready("terminal-output", CallResult(std::string(R"({"output":"remote terminal output","truncated":false})")));
        return ready("terminal-release", CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  static_cast<void>(terminal_service.handle_request(initialize_request_with_capabilities(R"({"terminal":true})"), {}));
  auto terminal_created = terminal_service.handle_request(
      Request{.id = std::int64_t(10), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto terminal_session = terminal_created ? ava::core::json::string_field(*terminal_created, "sessionId") : std::nullopt;
  RequestResult terminal_prompt;
  if (terminal_session)
    terminal_prompt = terminal_service.handle_request(
        Request{.id = std::int64_t(11),
                .method = "session/prompt",
                .params_json = std::string("{\"sessionId\":\"") + *terminal_session + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"run\"}]}"},
        {});
  std::vector<std::string> terminal_bodies;
  {
    std::lock_guard lock(terminal_state->mutex);
    terminal_bodies = terminal_state->request_bodies;
  }
  expect(terminal_session && terminal_prompt && terminal_bodies.size() == 2 && terminal_bodies.front().find("\"name\":\"bash\"") != std::string::npos &&
             terminal_bodies.back().find("remote terminal output") != std::string::npos && terminal_bodies.back().find("4294967295") != std::string::npos &&
             terminal_methods ==
                 std::vector<std::string>({"session/request_permission", "terminal/create", "terminal/wait_for_exit", "terminal/output", "terminal/release"}) &&
             !std::filesystem::exists(marker),
         "terminal negotiation exposes bash and routes exact create-wait-output-release without local process fallback");
  terminal_service.shutdown();

  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_builtin_permission_gateway_one_shot_mutations_and_updates()
{
  using namespace ava::app::acp;
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-permission-tools");
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_tool_test_model(root);
  auto paths = ava::tests::app_test_paths(root);
  auto state = std::make_shared<CapturingSequenceState>();
  auto const arguments = R"({\"path\":\"approved.txt\",\"content\":\"approved content\"})";
  auto tool_response = ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = std::string(R"({"choices":[{"message":{"tool_calls":[{"id":"call_write_1","function":{"name":"write_file","arguments":")") + arguments +
              R"("}},{"id":"call_write_2","function":{"name":"write_file","arguments":")" + arguments + R"("}}]},"finish_reason":"tool_calls"}]})"};

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = sequence_bundle_factory(state, {std::move(tool_response), acp_text_response("permission complete")});
  AgentService service(options);
  std::mutex sequence_mutex;
  std::vector<std::string> sequence;
  std::vector<std::string> permission_params;
  service.bind_update_sender([&](std::string_view, std::string_view update) -> ava::core::VoidResult {
    std::lock_guard lock(sequence_mutex);
    sequence.push_back(std::string("update:") + std::string(update));
    return {};
  });
  std::atomic_int request_sequence = 0;
  service.bind_client_request_sender(
      [&](std::string method, std::optional<std::string> params, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        expect(method == "session/request_permission" && params.has_value(), "ACP built-in tool asks through the exact client permission method");
        {
          std::lock_guard lock(sequence_mutex);
          sequence.push_back("permission");
          permission_params.push_back(params.value_or(""));
        }
        std::promise<CallResult> promise;
        promise.set_value(std::string(R"({"outcome":{"outcome":"selected","optionId":"allow_once"}})"));
        return PendingCall{.id = std::string("permission-") + std::to_string(++request_sequence), .completion = promise.get_future()};
      },
      [](JsonRpcId const&, std::string) { return true; });
  static_cast<void>(service.handle_request(initialize_request(), {}));
  auto created = service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  RequestResult prompted;
  if (id)
    prompted =
        service.handle_request(Request{.id = std::int64_t(3),
                                       .method = "session/prompt",
                                       .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"write twice\"}]}"},
                               {});
  service.shutdown();

  std::vector<std::string> observed;
  std::vector<std::string> requests;
  {
    std::lock_guard lock(sequence_mutex);
    observed = sequence;
    requests = permission_params;
  }
  auto const first_tool =
      std::ranges::find_if(observed, [](std::string const& item) { return item.find(R"("sessionUpdate":"tool_call")") != std::string::npos; });
  auto const permission = std::ranges::find(observed, "permission");
  auto const execution_start = std::ranges::find_if(observed, [](std::string const& item) {
    return item.find(R"("toolCallId":"call_write_1")") != std::string::npos && item.find(R"("status":"in_progress")") != std::string::npos;
  });
  auto const tool_result = std::ranges::find_if(observed, [](std::string const& item) { return item.find(R"("status":"completed")") != std::string::npos; });
  auto const agent_text = std::ranges::find_if(observed, [](std::string const& item) { return item.find("agent_message_chunk") != std::string::npos; });
  expect(id && prompted && *prompted == R"({"stopReason":"end_turn"})" && request_sequence.load() == 2 && requests.size() == 2 &&
             requests.front().find(R"("toolCallId":"call_write_1")") != std::string::npos &&
             requests.back().find(R"("toolCallId":"call_write_2")") != std::string::npos &&
             requests.front().find(R"("status":"pending")") != std::string::npos && requests.front().find("approved content") != std::string::npos &&
             requests.front().find(R"("optionId":"allow_always")") == std::string::npos && first_tool != observed.end() && permission != observed.end() &&
             execution_start != observed.end() && first_tool->find(R"("status":"pending")") != std::string::npos &&
             std::filesystem::exists(workspace / "approved.txt") && read_acp_test_file(workspace / "approved.txt") == "approved content" &&
             first_tool < permission && permission < execution_start && execution_start < tool_result && tool_result < agent_text,
         "ACP built-in writes show each bounded mutation and order pending, one-shot permission, in-progress, and completion exactly");

  if (id)
  {
    auto store = ava::session::SessionStore::open(workspace, *id, paths.sessions_dir);
    auto entries = store ? store->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(store.error()));
    bool audited = false;
    if (entries)
      for (auto const& entry : *entries)
        if (entry.type == ava::session::EntryType::PermissionDecision && ava::core::json::string_field(entry.data_json, "resolution_source") == "client")
          audited = true;
    expect(audited, "ACP one-shot file permission decisions persist the generic client source and permission/tool identity in the normal session audit stream");
  }
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_session_grant_cannot_follow_retargeted_parent_symlink()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-grant-retarget");
  auto const workspace = root / "workspace";
  auto const outside = root / "outside";
  std::filesystem::create_directories(workspace / "grant-parent");
  std::filesystem::create_directories(outside);
  configure_acp_tool_test_model(root);
  auto const paths = ava::tests::app_test_paths(root);
  auto const first_args = R"({\"path\":\"grant-parent/target.txt\",\"content\":\"inside approved\"})";
  auto const second_args = R"({\"path\":\"grant-parent/target.txt\",\"content\":\"outside denied\"})";
  auto tool_response = [](std::string_view id, std::string_view arguments) {
    return ava::provider::HttpResponse{.status_code = 200,
                                       .headers = {},
                                       .body = std::string(R"({"choices":[{"message":{"tool_calls":[{"id":")") + std::string(id) +
                                               R"(","function":{"name":"write_file","arguments":")" + std::string(arguments) +
                                               R"("}}]},"finish_reason":"tool_calls"}]})"};
  };
  auto provider_state = std::make_shared<CapturingSequenceState>();
  std::atomic_int bundle_number = 0;
  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = paths;
  options.provider_bundle_factory = [&, provider_state](ava::app::runtime::Session const& session, ava::app::runtime::RunOptions run_options,
                                                        std::string_view label) -> ava::core::Result<ava::app::RuntimeProviderRunBundle> {
    auto const current = bundle_number.fetch_add(1);
    auto responses = current == 0 ? std::vector<ava::provider::HttpResponse>{tool_response("grant_first", first_args), acp_text_response("first complete")}
                                  : std::vector<ava::provider::HttpResponse>{tool_response("grant_second", second_args), acp_text_response("second complete")};
    auto factory = sequence_bundle_factory(provider_state, std::move(responses));
    return factory(session, std::move(run_options), label);
  };
  AgentService service(options);
  service.bind_update_sender([](std::string_view, std::string_view) -> ava::core::VoidResult { return {}; });
  std::atomic_int permission_requests = 0;
  service.bind_client_request_sender(
      [&](std::string, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        ++permission_requests;
        std::promise<CallResult> promise;
        promise.set_value(std::string(R"({"outcome":{"outcome":"selected","optionId":"allow_once"}})"));
        return PendingCall{.id = std::string("grant-permission-") + std::to_string(permission_requests.load()), .completion = promise.get_future()};
      },
      [](JsonRpcId const&, std::string) { return true; });
  static_cast<void>(service.handle_request(initialize_request(), {}));
  auto created = service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  RequestResult first;
  if (session_id)
    first = service.handle_request(
        Request{.id = std::int64_t(3),
                .method = "session/prompt",
                .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"first\"}]}"},
        {});
  std::filesystem::rename(workspace / "grant-parent", workspace / "grant-parent-original");
  std::error_code link_error;
  std::filesystem::create_directory_symlink(outside, workspace / "grant-parent", link_error);
  RequestResult second;
  if (session_id && !link_error)
    second = service.handle_request(
        Request{.id = std::int64_t(4),
                .method = "session/prompt",
                .params_json = std::string("{\"sessionId\":\"") + *session_id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"second\"}]}"},
        {});
  expect(session_id && first && second && permission_requests.load() == 1 && !std::filesystem::exists(outside / "target.txt") &&
             read_acp_test_file(workspace / "grant-parent-original" / "target.txt") == "inside approved",
         "ACP one-shot mutation approvals cannot authorize a later path retargeted through a symlink");
  service.shutdown();
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_permission_once_always_reject_cancel_invalid_and_hard_policy_matrix()
{
  using namespace ava::app::acp;

  auto run_case = [](std::string_view option_id, bool cancelled, bool hard_deny, int expected_requests, bool expect_file, bool expect_cancelled) {
    auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-permission-matrix");
    auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    configure_acp_tool_test_model(root);
    auto paths = ava::tests::app_test_paths(root);
    auto provider_state = std::make_shared<CapturingSequenceState>();
    auto const target = hard_deny ? std::string(".env") : std::string("matrix.txt");
    auto const args = std::string("{\\\"path\\\":\\\"") + target + "\\\",\\\"content\\\":\\\"matrix-secret\\\"}";
    auto const tool_body = std::string(R"({"choices":[{"message":{"tool_calls":[{"id":"matrix_1","function":{"name":"write_file","arguments":")") + args +
                           R"("}},{"id":"matrix_2","function":{"name":"write_file","arguments":")" + args + R"("}}]},"finish_reason":"tool_calls"}]})";
    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = paths;
    options.provider_bundle_factory = sequence_bundle_factory(
        provider_state, {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = tool_body}, acp_text_response("matrix complete")});
    AgentService service(options);
    std::atomic_bool execution_started = false;
    service.bind_update_sender([&](std::string_view, std::string_view update) -> ava::core::VoidResult {
      if (update.find(R"("status":"in_progress")") != std::string_view::npos)
        execution_started.store(true, std::memory_order_release);
      return {};
    });
    std::atomic_int requests = 0;
    service.bind_client_request_sender(
        [&](std::string, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
          ++requests;
          std::promise<CallResult> promise;
          if (cancelled)
            promise.set_value(std::string(R"({"outcome":{"outcome":"cancelled"}})"));
          else
            promise.set_value(std::string("{\"outcome\":{\"outcome\":\"selected\",\"optionId\":\"") + std::string(option_id) + "\"}}");
          return PendingCall{.id = std::string("matrix-permission-") + std::to_string(requests.load()), .completion = promise.get_future()};
        },
        [](JsonRpcId const&, std::string) { return true; });
    static_cast<void>(service.handle_request(initialize_request(), {}));
    auto created = service.handle_request(
        Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
    auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
    RequestResult result;
    if (id)
      result = service.handle_request(
          Request{.id = std::int64_t(3),
                  .method = "session/prompt",
                  .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"permission matrix\"}]}"},
          {});
    service.shutdown();
    bool const canceled_result = result && *result == R"({"stopReason":"cancelled"})";
    bool const completed_result = result && *result == R"({"stopReason":"end_turn"})";
    expect(id && requests.load() == expected_requests && std::filesystem::exists(workspace / target) == expect_file &&
               execution_started.load(std::memory_order_acquire) == expect_file && (expect_cancelled ? canceled_result : completed_result),
           "ACP permission matrix never reports execution started for denied, canceled, invalid, or hard-denied calls");
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
  };

  run_case("allow_once", false, false, 2, true, false);
  run_case("reject_once", false, false, 2, false, false);
  run_case("reject_always", false, false, 2, false, false);
  run_case("allow_once", true, false, 1, false, true);
  run_case("not_offered", false, false, 2, false, false);
  run_case("allow_always", false, true, 0, false, false);
}

void test_acp_close_timeout_is_internal_error_with_eventual_cleanup()
{
  using namespace ava::app::acp;
  auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-close-timeout");
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);
  std::string body;
  std::atomic_bool entered = false;
  std::atomic_bool release = false;
  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = ava::tests::app_test_paths(root);
  options.provider_bundle_factory = recording_bundle_factory(&body, &entered, &release);
  options.close_grace = 5ms;
  AgentService service(options);
  static_cast<void>(service.handle_request(initialize_request(), {}));
  auto created = service.handle_request(
      Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
  auto id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
  RequestResult prompt_result;
  std::jthread prompt_thread;
  if (id)
  {
    prompt_thread = std::jthread([&] {
      prompt_result =
          service.handle_request(Request{.id = std::int64_t(3),
                                         .method = "session/prompt",
                                         .params_json = std::string("{\"sessionId\":\"") + *id + "\",\"prompt\":[{\"type\":\"text\",\"text\":\"block\"}]}"},
                                 {});
    });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
    auto closed =
        service.handle_request(Request{.id = std::int64_t(4), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *id + "\"}"}, {});
    expect(!closed && closed.error().code == -32603, "session/close reports internal stop timeout as -32603, not resource-not-found");
    release.store(true, std::memory_order_release);
    prompt_thread.join();
    auto absent =
        service.handle_request(Request{.id = std::int64_t(5), .method = "session/close", .params_json = std::string("{\"sessionId\":\"") + *id + "\"}"}, {});
    expect(!absent && absent.error().code == -32002, "timed-out close removes registry ownership and eventually releases host resources");
  }
  service.shutdown();
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_peer_prompt_terminal_commit_arbitration()
{
  using namespace ava::app::acp;

  auto run_case = [](ava::app::RunPhase barrier_phase, bool cancel_wins) {
    auto root = std::filesystem::temp_directory_path() / ava::core::make_id(cancel_wins ? "acp-peer-cancel-before" : "acp-peer-cancel-late");
    auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    configure_acp_test_model(root);
    auto provider_state = std::make_shared<CapturingSequenceState>();
    auto barrier = std::make_shared<RunPhaseBarrier>();
    barrier->target = barrier_phase;

    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = ava::tests::app_test_paths(root);
    options.run_options.on_phase = [barrier](ava::app::RunPhase phase) { return barrier->observe(phase); };
    options.provider_bundle_factory = sequence_bundle_factory(provider_state, {acp_text_response("peer terminal success")});
    AgentService service(options);
    auto state = std::make_shared<MemoryTransportState>();
    std::atomic_bool reader_probe = false;
    JsonRpcPeer peer(
        std::make_unique<MemoryTransport>(state), [&service](Request const& request, std::stop_token token) { return service.handle_request(request, token); },
        [&service, &reader_probe](Notification const& notification, std::stop_token token) {
          if (notification.method == "test/reader_probe")
            reader_probe.store(true, std::memory_order_release);
          else
            service.handle_notification(notification, token);
        });
    peer.set_request_pre_admission_hook([&service](Request const& request) { return service.pre_admit_request(request); });
    service.bind_request_terminal_committer([&peer](JsonRpcId const& id) { return peer.commit_inbound_request(id); });
    service.bind_update_sender([&peer](std::string_view session_id, std::string_view update) -> ava::core::VoidResult {
      return peer.send_notification("session/update",
                                    std::string("{\"sessionId\":\"") + ava::core::json::escape(session_id) + "\",\"update\":" + std::string(update) + "}");
    });

    ava::core::VoidResult run_result;
    std::jthread peer_thread([&] { run_result = peer.run(); });
    wait_reader(state);
    feed(state, R"({"jsonrpc":"2.0","id":"init","method":"initialize","params":{"protocolVersion":1}})");
    static_cast<void>(take_output(state));
    feed(state,
         std::string("{\"jsonrpc\":\"2.0\",\"id\":\"new\",\"method\":\"session/new\",\"params\":{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}}");
    auto created_record = take_output(state);
    auto created_result = created_record ? ava::core::json::object_field(*created_record, "result") : std::nullopt;
    auto session_id = created_result ? ava::core::json::string_field(*created_result, "sessionId") : std::nullopt;
    expect(session_id.has_value(), "ACP peer terminal arbitration creates a live session");
    if (!session_id)
    {
      close_input(state);
      peer_thread.join();
      return;
    }

    feed(state, std::string("{\"jsonrpc\":\"2.0\",\"id\":\"prompt\",\"method\":\"session/prompt\",\"params\":{\"sessionId\":\"") + *session_id +
                    "\",\"prompt\":[{\"type\":\"text\",\"text\":\"race\"}]}}");
    bool const reached = barrier->wait_until_reached();
    expect(reached, cancel_wins ? "ACP peer reaches a pre-terminal-commit boundary" : "ACP peer reaches the committed Completing boundary");
    feed(state, R"({"jsonrpc":"2.0","method":"$/cancel_request","params":{"requestId":"prompt"}})");
    feed(state, R"({"jsonrpc":"2.0","method":"test/reader_probe","params":{}})");
    auto const probe_deadline = std::chrono::steady_clock::now() + 2s;
    while (!reader_probe.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < probe_deadline) std::this_thread::sleep_for(1ms);
    barrier->release();

    std::optional<std::string> prompt_terminal;
    for (int index = 0; index < 4 && !prompt_terminal; ++index)
    {
      auto record = take_output(state);
      if (record && record->find(R"("id":"prompt")") != std::string::npos)
        prompt_terminal = std::move(record);
    }
    close_input(state);
    peer_thread.join();
    service.unbind_request_terminal_committer();
    service.unbind_update_sender();
    service.shutdown();

    std::size_t provider_requests = 0;
    {
      std::lock_guard lock(provider_state->mutex);
      provider_requests = provider_state->request_bodies.size();
    }
    auto store = ava::session::SessionStore::open(workspace, *session_id, options.paths.sessions_dir);
    auto entries = store ? store->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(store.error()));
    auto const assistant_count =
        entries ? std::count_if(entries->begin(), entries->end(), [](auto const& entry) { return entry.type == ava::session::EntryType::AssistantMessage; })
                : 0;
    if (cancel_wins)
    {
      expect(output_has_code(prompt_terminal, -32800) && peer.stats().canceled_inbound_requests == 1 && provider_requests == 0 && assistant_count == 0,
             "$/cancel_request before terminal commit wins with one canceled response and no durable/provider completion");
    }
    else
    {
      expect(prompt_terminal && prompt_terminal->find(R"("stopReason":"end_turn")") != std::string::npos && peer.stats().canceled_inbound_requests == 0 &&
                 provider_requests == 1 && assistant_count == 1,
             "$/cancel_request after runtime Completing loses and preserves the one successful PromptResponse and durable assistant");
    }
    expect(run_result.has_value(), "ACP peer terminal arbitration shuts down cleanly");
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
  };

  run_case(ava::app::RunPhase::AwaitingProvider, true);
  run_case(ava::app::RunPhase::Completing, false);
}

void test_acp_client_tool_dtos_lifecycle_and_cancellation()
{
  using namespace ava::app::acp;

  auto ready_call = [](std::string id, CallResult result) {
    std::promise<CallResult> promise;
    promise.set_value(std::move(result));
    return PendingCall{.id = std::move(id), .completion = promise.get_future()};
  };

  auto gateway = std::make_shared<ClientRequestGateway>();
  std::vector<std::string> methods;
  std::vector<std::string> params;
  std::vector<OutboundCallPolicy> policies;
  gateway->bind(
      [&](std::string method, std::optional<std::string> value, std::chrono::milliseconds, OutboundCallPolicy policy) -> ava::core::Result<PendingCall> {
        methods.push_back(method);
        params.push_back(value.value_or(""));
        policies.push_back(policy);
        if (method == "fs/read_text_file")
          return ready_call("read", CallResult(std::string(R"({"content":"remote text","_meta":[]})")));
        if (method == "fs/write_text_file")
          return ready_call("write", CallResult(std::string(R"({"_meta":7})")));
        if (method == "terminal/create")
          return ready_call("create", CallResult(std::string(R"({"terminalId":"terminal-1","_meta":[]})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("wait", CallResult(std::string(R"({"exitCode":0,"signal":null,"_meta":"ignored"})")));
        if (method == "terminal/output")
          return ready_call(
              "output",
              CallResult(std::string(R"({"output":"one\ntwo\n","truncated":false,"exitStatus":{"exitCode":0,"signal":null,"_meta":7},"_meta":false})")));
        return ready_call("release", CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });

  auto files = make_client_exact_file_access("session-a", gateway);
  auto read = files->read_text_file(std::filesystem::path("/workspace/note.txt"), nullptr);
  auto written = files->write_text_file(std::filesystem::path("/workspace/note.txt"), "updated", nullptr);
  auto windowed = files->read_text_file_window(std::filesystem::path("/workspace/note.txt"), {.line = 9, .limit = 21}, nullptr);
  expect(
      read && *read == "remote text" && written && windowed && *windowed == "remote text" && methods.size() == 3 && methods[0] == "fs/read_text_file" &&
          methods[1] == "fs/write_text_file" && methods[2] == "fs/read_text_file" && params[0].find(R"("sessionId":"session-a")") != std::string::npos &&
          params[0].find(R"("path":"/workspace/note.txt")") != std::string::npos && params[0].find(R"("line")") == std::string::npos &&
          params[1].find(R"("content":"updated")") != std::string::npos && ava::core::json::integer_field(params[2], "line") == 9 &&
          ava::core::json::integer_field(params[2], "limit") == 21 &&
          policies == std::vector<OutboundCallPolicy>({OutboundCallPolicy::Normal, OutboundCallPolicy::AbortConnectionIfDelivered, OutboundCallPolicy::Normal}),
      "ACP exact-file adapter emits exact full/windowed DTOs, ignores malformed optional response _meta, and uses fail-stop delivery only for writes");

  auto ambiguous_write_gateway = std::make_shared<ClientRequestGateway>();
  auto ambiguous_write_promise = std::make_shared<std::promise<CallResult>>();
  ambiguous_write_gateway->bind(
      [ambiguous_write_promise](std::string method, std::optional<std::string>, std::chrono::milliseconds,
                                OutboundCallPolicy policy) -> ava::core::Result<PendingCall> {
        expect(method == "fs/write_text_file" && policy == OutboundCallPolicy::AbortConnectionIfDelivered,
               "cancelable ACP file writes retain fail-stop policy at the gateway");
        return PendingCall{.id = std::string("ambiguous-write"), .completion = ambiguous_write_promise->get_future()};
      },
      [ambiguous_write_promise](JsonRpcId const&, std::string) {
        ambiguous_write_promise->set_value(std::unexpected(JsonRpcError{.code = -32603,
                                                                        .message = "ACP outbound request was delivered, but the response outcome is unknown",
                                                                        .data_json = std::nullopt,
                                                                        .id = std::nullopt,
                                                                        .intent = EnvelopeIntent::Response,
                                                                        .suppress_response = true}));
        return true;
      });
  auto ambiguous_write_files = make_client_exact_file_access("session-ambiguous-write", ambiguous_write_gateway);
  int ambiguous_write_cancel_checks = 0;
  auto ambiguous_write = ambiguous_write_files->write_text_file("/workspace/file.txt", "changed", [&] { return ++ambiguous_write_cancel_checks > 1; });
  expect(!ambiguous_write && ambiguous_write.error().format().find("outcome is unknown") != std::string::npos &&
             ambiguous_write.error().format().find("canceled: true") == std::string::npos,
         "ACP file-write cancellation boundedly surfaces the peer's ambiguous-delivery completion");

  auto ambiguous_create_gateway = std::make_shared<ClientRequestGateway>();
  auto ambiguous_create_promise = std::make_shared<std::promise<CallResult>>();
  ambiguous_create_gateway->bind(
      [ambiguous_create_promise](std::string method, std::optional<std::string>, std::chrono::milliseconds,
                                 OutboundCallPolicy policy) -> ava::core::Result<PendingCall> {
        expect(method == "terminal/create" && policy == OutboundCallPolicy::AbortConnectionIfDelivered,
               "terminal/create uses fail-stop delivery before acquiring its cleanup identity");
        return PendingCall{.id = std::string("ambiguous-create"), .completion = ambiguous_create_promise->get_future()};
      },
      [ambiguous_create_promise](JsonRpcId const&, std::string) {
        ambiguous_create_promise->set_value(std::unexpected(JsonRpcError{.code = -32603,
                                                                         .message = "ACP outbound request was delivered, but the response outcome is unknown",
                                                                         .data_json = std::nullopt,
                                                                         .id = std::nullopt,
                                                                         .intent = EnvelopeIntent::Response,
                                                                         .suppress_response = true}));
        return true;
      });
  auto ambiguous_create_commands = make_client_command_executor("session-ambiguous-create", ambiguous_create_gateway);
  int ambiguous_create_cancel_checks = 0;
  auto ambiguous_create = ambiguous_create_commands->execute(
      ava::tools::CommandExecutionRequest{.argv = {"sleep", "30"}, .cwd = "/workspace", .timeout = 1s, .output_byte_limit = 1024, .cancel_requested = [&] {
                                            return ++ambiguous_create_cancel_checks > 1;
                                          }});
  expect(!ambiguous_create && ambiguous_create.error().format().find("outcome is unknown") != std::string::npos,
         "ACP terminal/create cancellation surfaces delivered ambiguity through the fail-stop request policy");

  methods.clear();
  params.clear();
  policies.clear();
  auto commands = make_client_command_executor("session-a", gateway);
  auto executed = commands->execute(
      ava::tools::CommandExecutionRequest{.argv = {"printf", "hello world"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 4096});
  expect(executed && executed->exit_code == 0 && executed->output == "one\ntwo\n" &&
             methods == std::vector<std::string>({"terminal/create", "terminal/wait_for_exit", "terminal/output", "terminal/release"}) &&
             params[0].find(R"("command":"printf")") != std::string::npos && params[0].find(R"("args":["hello world"])") != std::string::npos &&
             params[0].find(R"("env":[])") != std::string::npos && params[0].find(R"("cwd":"/workspace")") != std::string::npos &&
             std::all_of(std::next(params.begin()), params.end(), [](std::string const& value) { return value.find("terminal-1") != std::string::npos; }) &&
             policies == std::vector<OutboundCallPolicy>({OutboundCallPolicy::AbortConnectionIfDelivered, OutboundCallPolicy::Normal,
                                                          OutboundCallPolicy::Normal, OutboundCallPolicy::Normal}),
         "ACP command adapter fail-stops ambiguous create ownership and otherwise uses exact create-wait-output-release ordering");

  auto const truncated_tail_fixture = read_acp_test_file(std::filesystem::path(AVA_ACP_V1_FIXTURE_DIR) / "terminal-output-truncated-tail.json");
  auto truncated_tail_gateway = std::make_shared<ClientRequestGateway>();
  truncated_tail_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "terminal/create")
          return ready_call("truncated-create", CallResult(std::string(R"({"terminalId":"terminal-truncated"})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("truncated-wait", CallResult(std::string(R"({"exitCode":0,"signal":null})")));
        if (method == "terminal/output")
          return ready_call("truncated-output", CallResult(truncated_tail_fixture));
        return ready_call("truncated-release", CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto truncated_tail_commands = make_client_command_executor("session-truncated-tail", truncated_tail_gateway);
  auto const truncated_root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-terminal-truncated-tail");
  auto const truncated_workspace = truncated_root / "workspace";
  std::filesystem::create_directories(truncated_workspace);
  std::vector<ava::tools::ToolProgressEvent> truncated_progress;
  ava::tools::ToolContext truncated_context{
      .workspace_dir = std::filesystem::canonical(truncated_workspace),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .progress_sink = [&truncated_progress](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
        truncated_progress.push_back(event);
        return {};
      },
      .command_executor = truncated_tail_commands};
  auto truncated_bash =
      ava::tools::run_bash(truncated_context, "printf retained-tail", ava::tools::BashOptions{.timeout = 100ms, .max_bytes = 4096, .max_lines = 200});
  auto const retained_tail = std::string("retained-tail-two\nretained-tail-three\n");
  expect(truncated_bash && truncated_bash->exit_code == 0 && !truncated_bash->timed_out && !truncated_bash->canceled && truncated_bash->truncated &&
             truncated_bash->byte_limited && !truncated_bash->line_limited && !truncated_bash->totals_known && truncated_bash->output == retained_tail &&
             truncated_bash->output_bytes == retained_tail.size() && truncated_bash->output_lines == 2 && truncated_bash->total_bytes == 0 &&
             truncated_bash->total_lines == 0 && truncated_bash->omitted_lines == 0 &&
             std::ranges::any_of(truncated_progress,
                                 [](ava::tools::ToolProgressEvent const& event) {
                                   return event.text.find("retained output bytes; original total unknown") != std::string::npos;
                                 }),
         "ACP terminal/output truncated-tail fixture preserves retained counts and status while clearing unavailable original totals");

  ava::agent::ToolDispatcher truncated_dispatcher(truncated_context);
  auto truncated_dispatch = truncated_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_truncated_terminal", .name = "bash", .arguments_json = R"({"command":"printf retained-tail","timeout_ms":100})"});
  auto const truncated_structured = truncated_dispatch ? ava::agent::serialize_tool_result_payload_json(*truncated_dispatch) : std::string{};
  auto const truncated_summary = truncated_dispatch ? ava::agent::summarize_tool_result(*truncated_dispatch) : std::string{};
  expect(truncated_dispatch && truncated_dispatch->success && ava::core::json::string_field(truncated_dispatch->result_text, "output") == retained_tail &&
             ava::core::json::integer_field(truncated_dispatch->result_text, "output_bytes") == static_cast<long long>(retained_tail.size()) &&
             ava::core::json::integer_field(truncated_dispatch->result_text, "output_lines") == 2 &&
             !ava::core::json::integer_field(truncated_dispatch->result_text, "total_bytes") &&
             !ava::core::json::integer_field(truncated_dispatch->result_text, "total_lines") &&
             !ava::core::json::integer_field(truncated_dispatch->result_text, "omitted_lines") &&
             truncated_summary.find("retained lines; original total unknown") != std::string::npos &&
             truncated_structured.find("\"total_bytes\"") == std::string::npos && truncated_structured.find("\"total_lines\"") == std::string::npos &&
             truncated_structured.find("\"omitted_lines\"") == std::string::npos,
         std::string(
             "ACP truncated terminal tool dispatch omits unknown totals from provider and structured serialization without losing retained counts: result=") +
             (truncated_dispatch ? truncated_dispatch->result_text : truncated_dispatch.error().format()) + " structured=" + truncated_structured +
             " summary=" + truncated_summary);
  std::error_code truncated_cleanup;
  std::filesystem::remove_all(truncated_root, truncated_cleanup);

  int response_cancel_checks = 0;
  auto response_wins = commands->execute(ava::tools::CommandExecutionRequest{
      .argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024, .cancel_requested = [&response_cancel_checks] {
        return ++response_cancel_checks > 1;
      }});
  expect(response_wins && response_wins->exit_code == 0 && !response_wins->canceled && response_cancel_checks == 1,
         "a ready terminal response wins before a newly observable cancellation can retire its pending call");

  auto cancel_gateway = std::make_shared<ClientRequestGateway>();
  std::vector<std::string> cancellation_order;
  std::vector<std::shared_ptr<std::promise<CallResult>>> held;
  cancel_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        cancellation_order.push_back(method);
        if (method == "terminal/wait_for_exit")
        {
          auto promise = std::make_shared<std::promise<CallResult>>();
          auto future = promise->get_future();
          held.push_back(std::move(promise));
          return PendingCall{.id = std::string("active-wait"), .completion = std::move(future)};
        }
        if (method == "terminal/create")
          return ready_call("create-cancel", CallResult(std::string(R"({"terminalId":"terminal-cancel"})")));
        if (method == "terminal/output")
          return ready_call("output-cancel", CallResult(std::string(R"({"output":"final","truncated":false})")));
        return ready_call(method, CallResult(std::string("{}")));
      },
      [&](JsonRpcId const& id, std::string) {
        expect(std::get<std::string>(id) == "active-wait", "ACP terminal cancellation targets the active wait request ID");
        cancellation_order.push_back("$/cancel_request");
        return true;
      });
  auto cancel_commands = make_client_command_executor("session-cancel", cancel_gateway);
  int cancel_checks = 0;
  auto canceled = cancel_commands->execute(ava::tools::CommandExecutionRequest{
      .argv = {"sleep", "30"}, .cwd = "/workspace", .timeout = 1s, .output_byte_limit = 1024, .cancel_requested = [&cancel_checks] {
        return ++cancel_checks >= 2;
      }});
  expect(canceled && canceled->canceled && canceled->exit_code == -1 &&
             cancellation_order == std::vector<std::string>({"terminal/create", "terminal/wait_for_exit", "$/cancel_request", "terminal/kill",
                                                             "terminal/output", "terminal/release"}),
         "ACP terminal cancellation sends $/cancel_request before kill, then fetches output and releases exactly once");

  auto release_gateway = std::make_shared<ClientRequestGateway>();
  release_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "terminal/create")
          return ready_call("create-release", CallResult(std::string(R"({"terminalId":"terminal-release"})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("wait-release", CallResult(std::string(R"({"exitCode":0,"signal":null})")));
        if (method == "terminal/output")
          return ready_call("output-release", CallResult(std::string(R"({"output":"ok","truncated":false})")));
        return ready_call("release-error", std::unexpected(JsonRpcError{.code = -32603,
                                                                        .message = "release failed",
                                                                        .data_json = std::nullopt,
                                                                        .id = std::nullopt,
                                                                        .intent = EnvelopeIntent::Response,
                                                                        .suppress_response = true}));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto release_commands = make_client_command_executor("session-release", release_gateway);
  auto unconfirmed_release =
      release_commands->execute(ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
  expect(!unconfirmed_release && unconfirmed_release.error().format().find("release failed") != std::string::npos &&
             unconfirmed_release.error().format().find("terminal_phase: release") != std::string::npos,
         "ACP terminal execution never reports clean success when release is unconfirmed");

  auto malformed_gateway = std::make_shared<ClientRequestGateway>();
  malformed_gateway->bind(
      [&](std::string, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        return ready_call("malformed", CallResult(std::string(R"({"content":7})")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto malformed_files = make_client_exact_file_access("session-malformed", malformed_gateway);
  auto malformed = malformed_files->read_text_file("/workspace/file.txt", nullptr);
  expect(!malformed && malformed.error().message().find("requires string content") != std::string::npos,
         "ACP client file adapter rejects malformed required response fields while allowing additive members");

  auto oversized_gateway = std::make_shared<ClientRequestGateway>();
  oversized_gateway->bind(
      [&](std::string, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        return ready_call("oversized", CallResult(std::string("{\"content\":\"") + std::string(kMaxStringBytes + 1, 'x') + "\"}"));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto oversized_files = make_client_exact_file_access("session-oversized", oversized_gateway);
  auto oversized = oversized_files->read_text_file("/workspace/file.txt", nullptr);
  expect(!oversized && oversized.error().message().find("string limit") != std::string::npos,
         "ACP client file adapter rejects oversized content even when a test gateway bypasses peer record validation");

  std::string large_client_file;
  for (std::size_t line = 1; line <= 4'000; ++line) large_client_file += "line-" + std::to_string(line) + '-' + std::string(80, 'x') + '\n';
  auto const window_root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-windowed-read");
  auto const window_workspace = window_root / "workspace";
  std::filesystem::create_directories(window_workspace);
  auto window_secure = ava::tools::SecureWorkspace::open(std::filesystem::canonical(window_workspace));
  auto window_gateway = std::make_shared<ClientRequestGateway>();
  std::vector<std::pair<std::size_t, std::size_t>> observed_windows;
  window_gateway->bind(
      [&](std::string method, std::optional<std::string> value, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        expect(method == "fs/read_text_file" && value.has_value(), "bounded tool reads use fs/read_text_file");
        auto const observed_line = static_cast<std::size_t>(ava::core::json::integer_field(value.value_or(""), "line").value_or(0));
        auto const observed_limit = static_cast<std::size_t>(ava::core::json::integer_field(value.value_or(""), "limit").value_or(0));
        observed_windows.emplace_back(observed_line, observed_limit);
        std::size_t begin = 0;
        for (std::size_t current = 1; current < observed_line && begin < large_client_file.size(); ++current)
        {
          auto const newline = large_client_file.find('\n', begin);
          begin = newline == std::string::npos ? large_client_file.size() : newline + 1;
        }
        auto end = begin;
        for (std::size_t count = 0; count < observed_limit && end < large_client_file.size(); ++count)
        {
          auto const newline = large_client_file.find('\n', end);
          end = newline == std::string::npos ? large_client_file.size() : newline + 1;
        }
        auto result = std::string("{\"content\":\"") + ava::core::json::escape(large_client_file.substr(begin, end - begin)) + "\"}";
        return ready_call("window", CallResult(std::move(result)));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto window_files = make_client_exact_file_access("session-window", window_gateway, true, false);
  ava::tools::ToolContext window_context{.workspace_dir = std::filesystem::canonical(window_workspace),
                                         .mode = ava::agent::Mode::Build,
                                         .secure_workspace = window_secure ? *window_secure : nullptr,
                                         .exact_file_access = window_files};
  auto bounded_window = ava::tools::read_file(window_context, window_workspace / "large.txt", {.max_bytes = 64 * 1024, .offset_line = 300, .max_lines = 200});
  ava::agent::ToolDispatcher window_dispatcher(window_context);
  auto dispatched_window = window_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_window", .name = "read_file", .arguments_json = R"({"path":"large.txt","offset":300,"limit":200,"max_bytes":65536})"});
  auto beyond_eof = ava::tools::read_file(window_context, window_workspace / "large.txt", {.max_bytes = 64 * 1024, .offset_line = 5'000, .max_lines = 200});
  auto dispatched_beyond = window_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_beyond_eof", .name = "read_file", .arguments_json = R"({"path":"large.txt","offset":5000,"limit":200,"max_bytes":65536})"});
  auto overflow_window = window_files->read_text_file_window(
      window_workspace / "large.txt", {.line = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1, .limit = 1}, nullptr);
  auto const dispatcher_window_totals_absent = dispatched_window && !ava::core::json::integer_field(dispatched_window->result_text, "total_bytes") &&
                                               !ava::core::json::integer_field(dispatched_window->result_text, "total_lines") &&
                                               ava::core::json::integer_field(dispatched_window->result_text, "output_lines") == 200 &&
                                               ava::core::json::integer_field(dispatched_window->result_text, "next_offset_line") == 500;
  auto const dispatcher_beyond_totals_absent = dispatched_beyond && !ava::core::json::integer_field(dispatched_beyond->result_text, "total_bytes") &&
                                               !ava::core::json::integer_field(dispatched_beyond->result_text, "total_lines") &&
                                               ava::core::json::integer_field(dispatched_beyond->result_text, "output_bytes") == 0 &&
                                               ava::core::json::integer_field(dispatched_beyond->result_text, "output_lines") == 0 &&
                                               ava::core::json::integer_field(dispatched_beyond->result_text, "start_line") == 5'000 &&
                                               ava::core::json::integer_field(dispatched_beyond->result_text, "end_line") == 0;
  expect(large_client_file.size() > kMaxStringBytes && window_secure && bounded_window && !bounded_window->totals_known && bounded_window->total_bytes == 0 &&
             bounded_window->total_lines == 0 && bounded_window->content.starts_with("line-300-") && bounded_window->output_lines == 200 &&
             bounded_window->line_limited && bounded_window->next_offset_line == 500 && dispatcher_window_totals_absent && beyond_eof &&
             !beyond_eof->totals_known && beyond_eof->content.empty() && beyond_eof->output_bytes == 0 && beyond_eof->output_lines == 0 &&
             beyond_eof->start_line == 5'000 && beyond_eof->end_line == 0 && beyond_eof->total_bytes == 0 && beyond_eof->total_lines == 0 &&
             !beyond_eof->line_limited && beyond_eof->next_offset_line == 0 && dispatcher_beyond_totals_absent &&
             observed_windows == std::vector<std::pair<std::size_t, std::size_t>>({{300, 201}, {300, 201}, {5'000, 201}, {5'000, 201}}) && !overflow_window,
         "ACP read_file uses unseen sentinel lines for continuation, leaves window totals unknown in dispatcher JSON, and never fabricates beyond-EOF totals");
  std::error_code window_cleanup;
  std::filesystem::remove_all(window_root, window_cleanup);

  auto const full_read_root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-full-edit-read");
  auto const full_read_workspace = full_read_root / "workspace";
  std::filesystem::create_directories(full_read_workspace);
  auto full_read_secure = ava::tools::SecureWorkspace::open(std::filesystem::canonical(full_read_workspace));
  auto full_read_gateway = std::make_shared<ClientRequestGateway>();
  std::string full_read_content = "alpha old stale\n";
  std::vector<std::string> full_read_params;
  full_read_gateway->bind(
      [&](std::string method, std::optional<std::string> value, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "fs/read_text_file")
        {
          full_read_params.push_back(value.value_or(""));
          return ready_call("full-read", CallResult(std::string("{\"content\":\"") + ava::core::json::escape(full_read_content) + "\"}"));
        }
        if (method == "fs/write_text_file")
        {
          full_read_content = ava::core::json::string_field(value.value_or(""), "content").value_or("");
          return ready_call("full-write", CallResult(std::string("{}")));
        }
        return std::unexpected(ava::app::acp::protocol_error("unexpected full-read test method"));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto full_read_files = make_client_exact_file_access("session-full-read", full_read_gateway, true, true);
  ava::tools::ToolContext full_read_context{.workspace_dir = std::filesystem::canonical(full_read_workspace),
                                            .mode = ava::agent::Mode::Build,
                                            .secure_workspace = full_read_secure ? *full_read_secure : nullptr,
                                            .exact_file_access = full_read_files};
  auto full_edit = ava::tools::edit_file(full_read_context, full_read_workspace / "remote.txt", "old", "new");
  ava::agent::ToolDispatcher full_patch_dispatcher(full_read_context);
  auto full_patch = full_patch_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "full-patch", .name = "apply_patch", .arguments_json = R"({"edits":[{"path":"remote.txt","old_text":"stale","new_text":"fresh"}]})"});
  expect(full_read_secure && full_edit && full_patch && full_patch->success && full_read_content == "alpha new fresh\n" && full_read_params.size() == 2 &&
             std::ranges::all_of(
                 full_read_params,
                 [](std::string const& value) { return !ava::core::json::integer_field(value, "line") && !ava::core::json::integer_field(value, "limit"); }),
         "ACP edit_file and apply_patch use full unwindowed fs/read_text_file DTOs");
  std::error_code full_read_cleanup;
  std::filesystem::remove_all(full_read_root, full_read_cleanup);

  auto range_gateway = std::make_shared<ClientRequestGateway>();
  int release_calls = 0;
  range_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "terminal/create")
          return ready_call("range-create", CallResult(std::string(R"({"terminalId":"terminal-range"})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("range-wait", CallResult(std::string(R"({"exitCode":4294967295,"signal":null})")));
        if (method == "terminal/output")
          return ready_call("range-output", CallResult(std::string(R"({"output":"","truncated":false})")));
        if (method == "terminal/release")
          ++release_calls;
        return ready_call(method, CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto range_commands = make_client_command_executor("session-range", range_gateway);
  auto range_result =
      range_commands->execute(ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
  expect(range_result && range_result->exit_code == 4'294'967'295LL && release_calls == 1,
         "ACP terminal DTO and lifecycle accept UINT32_MAX and release the acquired terminal exactly once");

  auto run_defaulted_status_case = [&](std::string_view name, std::string wait_json, std::string output_json, std::int64_t expected_exit,
                                       std::string_view expected_error = {}) {
    auto status_gateway = std::make_shared<ClientRequestGateway>();
    int status_releases = 0;
    status_gateway->bind(
        [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
          if (method == "terminal/create")
            return ready_call(std::string(name) + "-create", CallResult(std::string(R"({"terminalId":"terminal-defaulted","_meta":[]})")));
          if (method == "terminal/wait_for_exit")
            return ready_call(std::string(name) + "-wait", CallResult(wait_json));
          if (method == "terminal/output")
            return ready_call(std::string(name) + "-output", CallResult(output_json));
          if (method == "terminal/release")
            ++status_releases;
          return ready_call(std::string(name) + "-cleanup", CallResult(std::string(R"({"_meta":false})")));
        },
        [](JsonRpcId const&, std::string) { return true; });
    auto status_commands = make_client_command_executor(std::string("session-") + std::string(name), status_gateway);
    auto status_result =
        status_commands->execute(ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
    auto const matches = expected_error.empty() ? status_result && status_result->exit_code == expected_exit
                                                : !status_result && status_result.error().format().find(expected_error) != std::string::npos;
    expect(matches && status_releases == 1, std::string("ACP terminal field-local default case: ") + std::string(name));
  };

  run_defaulted_status_case("wait-valid-code-sibling", R"({"exitCode":7,"signal":9,"_meta":[]})",
                            R"({"output":"ok","truncated":false,"exitStatus":[],"_meta":7})", 7);
  run_defaulted_status_case("output-valid-code-sibling", R"({"exitCode":"bad","signal":7})",
                            R"({"output":"ok","truncated":false,"exitStatus":{"exitCode":5,"signal":{},"_meta":[]}})", 5);
  run_defaulted_status_case("unknown-status", R"({"exitCode":-1,"signal":7})", R"({"output":"ok","truncated":false,"exitStatus":"bad"})", -1);
  run_defaulted_status_case("required-output-fields", R"({"exitCode":0})", R"({"output":"missing truncated","_meta":[]})", -1,
                            "requires string output and boolean truncated");

  auto malformed_create_gateway = std::make_shared<ClientRequestGateway>();
  int malformed_create_calls = 0;
  bool malformed_create_aborted = false;
  malformed_create_gateway->bind(
      [&](std::string, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        ++malformed_create_calls;
        return ready_call("malformed-create", CallResult(std::string(R"({"terminalId":7,"_meta":[]})")));
      },
      [](JsonRpcId const&, std::string) { return true; }, [&](std::string) { malformed_create_aborted = true; });
  auto malformed_create_commands = make_client_command_executor("session-malformed-create", malformed_create_gateway);
  auto malformed_create = malformed_create_commands->execute(
      ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
  expect(
      !malformed_create && malformed_create.error().format().find("terminalId") != std::string::npos && malformed_create_calls == 1 && malformed_create_aborted,
      "ACP terminal/create keeps its required terminalId strict and aborts when delivered ownership cannot be recovered");

  auto malformed_status_gateway = std::make_shared<ClientRequestGateway>();
  int malformed_status_releases = 0;
  malformed_status_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "terminal/create")
          return ready_call("malformed-status-create", CallResult(std::string(R"({"terminalId":"terminal-malformed-status"})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("malformed-status-wait", CallResult(std::string(R"({"exitCode":0,"signal":"SIGKILL"})")));
        if (method == "terminal/output")
          return ready_call("malformed-status-output", CallResult(std::string(R"({"output":"","truncated":false})")));
        if (method == "terminal/release")
          ++malformed_status_releases;
        return ready_call(method, CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto malformed_status_commands = make_client_command_executor("session-malformed-status", malformed_status_gateway);
  auto malformed_status = malformed_status_commands->execute(
      ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
  expect(!malformed_status && malformed_status.error().format().find("both a valid exitCode and a valid signal") != std::string::npos &&
             malformed_status_releases == 1,
         "ACP terminal adapter rejects one status containing both exit kinds and releases exactly once");

  auto conflicting_status_gateway = std::make_shared<ClientRequestGateway>();
  int conflicting_status_releases = 0;
  conflicting_status_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "terminal/create")
          return ready_call("conflicting-status-create", CallResult(std::string(R"({"terminalId":"terminal-conflicting-status"})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("conflicting-status-wait", CallResult(std::string(R"({"exitCode":0,"signal":null})")));
        if (method == "terminal/output")
          return ready_call("conflicting-status-output",
                            CallResult(std::string(R"({"output":"","truncated":false,"exitStatus":{"exitCode":null,"signal":"SIGKILL"}})")));
        if (method == "terminal/release")
          ++conflicting_status_releases;
        return ready_call(method, CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto conflicting_status_commands = make_client_command_executor("session-conflicting-status", conflicting_status_gateway);
  auto conflicting_status = conflicting_status_commands->execute(
      ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
  expect(!conflicting_status && conflicting_status.error().format().find("exitStatus conflicts") != std::string::npos && conflicting_status_releases == 1,
         "ACP terminal adapter rejects cross-response exit-kind disagreement and releases exactly once");

  auto signal_status_gateway = std::make_shared<ClientRequestGateway>();
  int signal_status_releases = 0;
  signal_status_gateway->bind(
      [&](std::string method, std::optional<std::string>, std::chrono::milliseconds, OutboundCallPolicy) -> ava::core::Result<PendingCall> {
        if (method == "terminal/create")
          return ready_call("signal-status-create", CallResult(std::string(R"({"terminalId":"terminal-signal-status"})")));
        if (method == "terminal/wait_for_exit")
          return ready_call("signal-status-wait", CallResult(std::string(R"({"exitCode":"bad","signal":"SIGTERM"})")));
        if (method == "terminal/output")
          return ready_call("signal-status-output",
                            CallResult(std::string(R"({"output":"","truncated":false,"exitStatus":{"exitCode":{},"signal":"SIGTERM"}})")));
        if (method == "terminal/release")
          ++signal_status_releases;
        return ready_call(method, CallResult(std::string("{}")));
      },
      [](JsonRpcId const&, std::string) { return true; });
  auto signal_status_commands = make_client_command_executor("session-signal-status", signal_status_gateway);
  auto signal_status =
      signal_status_commands->execute(ava::tools::CommandExecutionRequest{.argv = {"true"}, .cwd = "/workspace", .timeout = 100ms, .output_byte_limit = 1024});
  expect(signal_status && signal_status->exit_code == -1 && signal_status_releases == 1,
         "ACP terminal adapter preserves valid signal siblings when malformed exitCode fields default absent and releases exactly once");
}

void test_acp_peer_lifecycle_notifications_and_duplicate_ids()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  AgentService service("1.0.0");
  JsonRpcPeer peer(
      std::make_unique<MemoryTransport>(state), [&service](Request const& request, std::stop_token token) { return service.handle_request(request, token); },
      [&service](Notification const& notification, std::stop_token token) { service.handle_notification(notification, token); });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);

  feed(state, R"({"jsonrpc":"2.0","id":1,"method":"session/new","params":{}})");
  expect(output_has_code(take_output(state), -32600), "ACP peer enforces pre-init gating");
  feed(state, R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":1}})");
  auto init = take_output(state);
  expect(init && init->find("\"result\"") != std::string::npos && init->find("\"protocolVersion\":1") != std::string::npos,
         "ACP peer successfully negotiates the M4 v1 baseline");
  feed(state, R"({"jsonrpc":"2.0","method":"unknown","params":{}})");
  expect(!take_output(state, 80ms), "ACP notification failures produce no response");
  feed(state, R"({"jsonrpc":"2.0","id":3,"method":"unknown","params":{}})");
  expect(output_has_code(take_output(state), -32601), "ACP peer routes unknown requests after initialization");
  feed(state, R"({"jsonrpc":"1.0","id":9,"result":{}})");
  feed(state, R"({"jsonrpc":"2.0","id":9,"error":{"code":"bad","message":1}})");
  feed(state, R"({"jsonrpc":"2.0","id":10})");
  feed(state, R"({"jsonrpc":"2.0","id":null})");
  feed(state, R"({"jsonrpc":"2.0","id":11,"result":{},"error":{"code":1,"message":"bad"}})");
  expect(!take_output(state, 80ms) && peer.stats().unknown_or_late_responses == 5,
         "all malformed response-intent envelopes are diagnosed and ignored without response loops");

  std::string deep_value(kMaxNestingDepth + 1, '[');
  deep_value += '0';
  deep_value.append(kMaxNestingDepth + 1, ']');
  feed(state, R"({"jsonrpc":"2.0","id":20,"result":)" + deep_value + '}');
  feed(state, R"({"jsonrpc":"2.0","result":)" + deep_value + R"(,"id":null})");
  feed_limit_error(state, EnvelopeIntent::Response);
  feed_limit_error(state, EnvelopeIntent::Response);
  feed_limit_error(state, EnvelopeIntent::Notification);
  expect(!take_output(state, 80ms) && peer.stats().unknown_or_late_responses == 9,
         "over-depth and oversized response hints, plus oversized notifications, remain response-free");
  feed_limit_error(state, EnvelopeIntent::Request);
  expect(output_has_code(take_output(state), -32700), "an oversized request hint still produces the bounded parse error");

  feed(state, R"({"jsonrpc":"2.0","id":12,"method":"unknown","params":{}})");
  expect(output_has_code(take_output(state), -32601), "ACP peer continues handling valid input after depth and size intent errors");

  close_input(state);
  thread.join();
  expect(run_result.has_value(), "ACP peer exits cleanly on EOF");
}

void test_acp_peer_bidirectional_out_of_order_deadline_and_late_response()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);

  auto first = peer.send_request("client/one", std::string("{}"), 2s);
  auto second = peer.send_request("client/two", std::string("{}"), 2s);
  expect(first && second, "ACP peer admits bounded outbound calls");
  static_cast<void>(take_output(state));
  static_cast<void>(take_output(state));
  if (first && second)
  {
    auto second_response = encode_success(second->id, R"({"order":2})");
    auto first_response = encode_success(first->id, R"({"order":1})");
    feed(state, *second_response);
    feed(state, *first_response);
    auto second_done = second->completion.get();
    auto first_done = first->completion.get();
    expect(second_done && second_done->find("2") != std::string::npos && first_done && first_done->find("1") != std::string::npos,
           "ACP peer correlates out-of-order responses by typed id");
  }

  auto canceled = peer.send_request("client/cancel", std::string("{}"), 2s);
  expect(canceled.has_value(), "ACP peer admits an outbound call that the active prompt may cancel");
  auto canceled_record = take_output(state);
  auto cancellation_barrier_sent = peer.send_notification("test/cancellation_delivery_barrier", std::string("{}"));
  auto cancellation_barrier_record = take_output(state);
  expect(canceled_record && cancellation_barrier_sent && cancellation_barrier_record,
         "a subsequent outbound barrier observes the normal request after writer-acknowledged delivery");
  if (canceled)
  {
    peer.cancel_pending_call(canceled->id, "prompt cancelled");
    auto cancellation = canceled->completion.get();
    expect(!cancellation && cancellation.error().code == -32800, "prompt cancellation releases one outbound client request without closing the connection");
    auto late_canceled = encode_success(canceled->id, R"({"outcome":{"outcome":"selected","optionId":"allow_always"}})");
    feed(state, *late_canceled);
    std::this_thread::sleep_for(20ms);
    expect(peer.stats().unknown_or_late_responses == 1, "a response arriving after prompt cancellation is ignored and cannot apply a permission grant");
  }

  auto timeout = peer.send_request("client/slow", std::string("{}"), 30ms);
  expect(timeout.has_value(), "ACP peer admits a finite-deadline call");
  static_cast<void>(take_output(state));
  if (timeout)
  {
    auto timed_out = timeout->completion.get();
    expect(!timed_out && timed_out.error().code == -32603 && timed_out.error().message.find("outcome is unknown") != std::string::npos,
           "a deadline after writer delivery reports an ambiguous response outcome rather than local rollback");
    auto late = encode_success(timeout->id, "{}");
    feed(state, *late);
    std::this_thread::sleep_for(20ms);
    expect(peer.stats().unknown_or_late_responses == 2, "ACP peer safely ignores and counts a late response");
  }
  feed(state, R"({"jsonrpc":"2.0","id":null,"result":{"unknown":true}})");
  feed(state, R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"unknown"}})");
  std::this_thread::sleep_for(20ms);
  expect(peer.stats().unknown_or_late_responses == 4, "null success and error responses preserve identity and follow bounded unknown-response handling");

  bool races_completed = true;
  for (int index = 0; index < 20; ++index)
  {
    auto call = peer.send_request("client/race", std::string("{}"), 3ms);
    if (!call)
    {
      races_completed = false;
      break;
    }
    static_cast<void>(take_output(state));
    auto response_record = encode_success(call->id, "{}");
    std::jthread responder([state, response_record, index] {
      std::this_thread::sleep_for(index % 2 == 0 ? 1ms : 3ms);
      feed(state, *response_record);
    });
    auto completion = call->completion.get();
    races_completed = races_completed && (completion.has_value() ||
                                          (completion.error().code == -32603 && completion.error().message.find("outcome is unknown") != std::string::npos));
  }
  expect(races_completed, "ACP response-versus-deadline races have exactly one pending-call completion owner");

  close_input(state);
  thread.join();
  expect(run_result.has_value(), "ACP bidirectional peer shuts down after interleaving");
}

void test_acp_peer_cancel_duplicate_inflight_and_saturation()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  std::atomic_int started = 0;
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [&started](Request const&, std::stop_token token) -> RequestResult {
    started.fetch_add(1, std::memory_order_release);
    while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
    return std::unexpected(JsonRpcError{.code = -32800, .message = "cancelled", .data_json = std::nullopt, .id = std::nullopt, .suppress_response = false});
  });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);

  feed(state, R"({"jsonrpc":"2.0","id":null,"method":"slow","params":{}})");
  while (started.load(std::memory_order_acquire) == 0) std::this_thread::sleep_for(1ms);
  feed(state, R"({"jsonrpc":"2.0","id":null,"method":"slow","params":{}})");
  expect(output_has_code(take_output(state), -32600), "ACP peer reserves explicit null ids and rejects a colliding in-flight request");
  feed(state, R"({"jsonrpc":"2.0","method":"$/cancel_request","params":{"requestId":null}})");
  auto null_cancellation = take_output(state);
  expect(output_has_code(null_cancellation, -32800) && null_cancellation->find("\"id\":null") != std::string::npos,
         "$/cancel_request correlates null and preserves it on the cancellation response");
  expect(peer.stats().duplicate_inbound_ids == 1 && peer.stats().canceled_inbound_requests == 1,
         "ACP peer records duplicate and cancellation routing outcomes");

  for (int index = 0; index <= static_cast<int>(kMaxInflightRequests); ++index)
    feed(state, "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(index) + ",\"method\":\"slow\",\"params\":{}}");
  expect(output_has_code(take_output(state), -32603), "ACP peer enforces the concurrent inbound request limit");
  for (int index = 0; index < static_cast<int>(kMaxInflightRequests); ++index)
    feed(state, "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancel_request\",\"params\":{\"requestId\":" + std::to_string(index) + "}}");
  bool inflight_released = true;
  for (std::size_t index = 0; index < kMaxInflightRequests; ++index) inflight_released = inflight_released && output_has_code(take_output(state), -32800);
  expect(inflight_released, "ACP cancellation releases every saturated in-flight slot");

  std::deque<PendingCall> pending;
  for (std::size_t index = 0; index < kMaxPendingCalls; ++index)
  {
    auto call = peer.send_request("client/pending", std::string("{}"), 5s);
    if (call)
      pending.push_back(std::move(*call));
  }
  auto saturated = peer.send_request("client/overflow", std::string("{}"), 5s);
  expect(pending.size() == kMaxPendingCalls && !saturated, "ACP peer enforces the pending outbound call limit");

  close_input(state);
  thread.join();
  bool all_released = true;
  for (auto& call : pending)
  {
    auto result = call.completion.get();
    all_released = all_released && !result &&
                   (result.error().code == -32800 || (result.error().code == -32603 && result.error().message.find("outcome is unknown") != std::string::npos));
  }
  expect(all_released && run_result.has_value(), "ACP EOF releases every pending outbound call with delivery-aware terminal errors");
}

void test_acp_peer_lifecycle_request_commit_linearization()
{
  using namespace ava::app::acp;
  auto const root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-peer-lifecycle-commit");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  configure_acp_test_model(root);

  AgentServiceOptions options;
  options.agent_version = "1";
  options.launch_root = std::filesystem::canonical(workspace);
  options.paths = ava::tests::app_test_paths(root);
  AgentService service(options);
  auto state = std::make_shared<MemoryTransportState>();
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state),
                   [&service](Request const& request, std::stop_token token) { return service.handle_request(request, token); });
  std::mutex barrier_mutex;
  std::condition_variable barrier_cv;
  bool committed = false;
  bool release = false;
  service.bind_request_terminal_committer([&](JsonRpcId const& id) {
    bool const accepted = peer.commit_inbound_request(id);
    auto const* text = std::get_if<std::string>(&id);
    if (text == nullptr || *text != "new-late" || !accepted)
      return accepted;
    std::unique_lock lock(barrier_mutex);
    committed = true;
    barrier_cv.notify_all();
    barrier_cv.wait(lock, [&] { return release; });
    return true;
  });

  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);
  feed(state, R"({"jsonrpc":"2.0","id":"init","method":"initialize","params":{"protocolVersion":1}})");
  auto initialized = take_output(state);
  expect(initialized && initialized->find("\"result\"") != std::string::npos, "ACP peer lifecycle fixture commits initialize successfully");

  feed(state, std::string("{\"jsonrpc\":\"2.0\",\"id\":\"new-late\",\"method\":\"session/new\",\"params\":{\"cwd\":\"") + workspace.string() +
                  "\",\"mcpServers\":[]}}");
  {
    std::unique_lock lock(barrier_mutex);
    static_cast<void>(barrier_cv.wait_for(lock, 2s, [&] { return committed; }));
  }
  feed(state, R"({"jsonrpc":"2.0","method":"$/cancel_request","params":{"requestId":"new-late"}})");
  std::this_thread::sleep_for(10ms);
  {
    std::lock_guard lock(barrier_mutex);
    release = true;
  }
  barrier_cv.notify_all();
  auto created = take_output(state);
  auto created_result = created ? ava::core::json::object_field(*created, "result") : std::nullopt;
  auto session_id = created_result ? ava::core::json::string_field(*created_result, "sessionId") : std::nullopt;
  expect(session_id && peer.stats().canceled_inbound_requests == 0,
         "late $/cancel_request loses after session/new commits and the actual successful mutation response is retained");

  close_input(state);
  thread.join();
  service.unbind_request_terminal_committer();
  service.shutdown();
  expect(run_result.has_value(), "lifecycle request commit linearization test shuts down cleanly");
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
}

void test_acp_peer_outbound_queue_saturation()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  state->block_writes = true;
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);

  expect(peer.send_notification("test/block", std::string("{}")).has_value(), "ACP saturation regression claims one blocked writer record");
  wait_writer(state);
  std::size_t queued = 0;
  for (std::size_t index = 0; index <= kMaxOutboundRecords; ++index)
  {
    auto sent = peer.send_notification("test/queued", std::string("{}"));
    if (!sent)
      break;
    ++queued;
  }
  expect(queued == kMaxOutboundRecords, "ACP peer deterministically fills the bounded outbound FIFO behind a blocked writer");

  auto const old_deadline = std::chrono::steady_clock::now() + 5s;
  std::size_t failed_requests = 0;
  for (std::size_t index = 0; index < kMaxPendingCalls; ++index)
  {
    auto failed = peer.send_request("client/saturated", std::string("{}"), 5s);
    if (!failed)
      ++failed_requests;
  }
  expect(failed_requests == kMaxPendingCalls, "every saturated request enqueue fails without consuming a pending-call slot");

  {
    std::lock_guard lock(state->mutex);
    state->block_writes = false;
    state->cv.notify_all();
  }
  bool drained = true;
  for (std::size_t index = 0; index < queued + 1; ++index) drained = drained && take_output(state).has_value();

  auto recovered = peer.send_request("client/recovered", std::string("{}"), 2s);
  auto recovered_record = take_output(state);
  if (recovered)
  {
    auto response = encode_success(recovered->id, R"({"recovered":true})");
    feed(state, *response);
  }
  bool const recovered_ready = recovered && recovered->completion.wait_for(2s) == std::future_status::ready;
  auto recovered_result = recovered_ready ? std::optional<CallResult>(recovered->completion.get()) : std::nullopt;
  expect(drained && recovered_record && recovered_result && recovered_result->has_value() && std::chrono::steady_clock::now() < old_deadline,
         "failed enqueue rollback leaves no dangling pending entries or futures and a later request completes before stale deadlines");

  close_input(state);
  thread.join();
  expect(run_result.has_value(), "ACP saturation rollback regression shuts down cleanly");
}

void test_acp_peer_delivered_fail_stop_cancellation()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);

  auto write = peer.send_request("fs/write_text_file", std::string("{}"), 2s, OutboundCallPolicy::AbortConnectionIfDelivered);
  auto write_record = take_output(state);
  auto barrier_sent = peer.send_notification("test/delivery_barrier", std::string("{}"));
  auto barrier_record = take_output(state);
  bool const canceled = write && peer.cancel_pending_call(write->id, "prompt canceled after file write delivery");
  bool const ready = write && write->completion.wait_for(2s) == std::future_status::ready;
  auto result = ready ? std::optional<CallResult>(write->completion.get()) : std::nullopt;
  auto rejected = peer.send_request("fs/write_text_file", std::string("{}"), 2s, OutboundCallPolicy::AbortConnectionIfDelivered);

  thread.join();
  expect(write_record && barrier_sent && barrier_record && canceled && result && !*result && result->error().code == -32603 &&
             result->error().message.find("outcome is unknown") != std::string::npos && state->canceled && state->cancel_calls > 0 && !rejected &&
             run_result.has_value(),
         "canceling a delivered fail-stop call returns ambiguous delivery, poisons the connection, and rejects later mutations");

  auto deadline_state = std::make_shared<MemoryTransportState>();
  JsonRpcPeer deadline_peer(std::make_unique<MemoryTransport>(deadline_state),
                            [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult deadline_run;
  std::jthread deadline_thread([&] { deadline_run = deadline_peer.run(); });
  wait_reader(deadline_state);
  auto deadline_write = deadline_peer.send_request("fs/write_text_file", std::string("{}"), 50ms, OutboundCallPolicy::AbortConnectionIfDelivered);
  auto deadline_write_record = take_output(deadline_state);
  auto deadline_barrier_sent = deadline_peer.send_notification("test/deadline_delivery_barrier", std::string("{}"));
  auto deadline_barrier_record = take_output(deadline_state);
  bool const deadline_ready = deadline_write && deadline_write->completion.wait_for(2s) == std::future_status::ready;
  auto deadline_result = deadline_ready ? std::optional<CallResult>(deadline_write->completion.get()) : std::nullopt;
  auto deadline_rejected = deadline_peer.send_request("fs/write_text_file", std::string("{}"), 2s, OutboundCallPolicy::AbortConnectionIfDelivered);
  deadline_thread.join();
  expect(deadline_write_record && deadline_barrier_sent && deadline_barrier_record && deadline_result && !*deadline_result &&
             deadline_result->error().code == -32603 && deadline_result->error().message.find("outcome is unknown") != std::string::npos &&
             deadline_state->canceled && !deadline_rejected && deadline_run.has_value(),
         "a delivered fail-stop deadline reports ambiguity, aborts the connection, and rejects later mutations");
}

void test_acp_peer_fail_stop_poison_arbitrates_all_pending_calls()
{
  using namespace ava::app::acp;

  auto run_case = [](bool deadline_triggered) {
    auto state = std::make_shared<MemoryTransportState>();
    std::mutex reader_barrier_mutex;
    std::condition_variable reader_barrier_cv;
    bool reader_barrier_reached = false;
    JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
    peer.set_control_notification_handler([&](Notification const&) {
      {
        std::lock_guard lock(reader_barrier_mutex);
        reader_barrier_reached = true;
      }
      reader_barrier_cv.notify_all();
    });
    ava::core::VoidResult run_result;
    std::jthread thread([&] { run_result = peer.run(); });
    wait_reader(state);

    auto trigger = peer.send_request("fs/write_text_file", std::string("{}"), deadline_triggered ? 1s : 5s, OutboundCallPolicy::AbortConnectionIfDelivered);
    auto trigger_record = take_output(state);
    auto delivery_barrier_sent = peer.send_notification("test/fail_stop_delivery_barrier", std::string("{}"));
    auto delivery_barrier_record = take_output(state);

    std::size_t claimed_write_attempt = 0;
    {
      std::lock_guard lock(state->mutex);
      state->block_writes = true;
      claimed_write_attempt = state->write_attempts + 1;
    }
    auto collateral = peer.send_request("fs/write_text_file", std::string("{}"), 5s, OutboundCallPolicy::AbortConnectionIfDelivered);
    bool const writer_claimed = wait_for_write_attempts(state, claimed_write_attempt);
    if (collateral)
    {
      auto staged_response = encode_success(collateral->id, R"({})");
      if (staged_response)
        feed(state, std::move(*staged_response));
    }
    feed(state, R"({"jsonrpc":"2.0","method":"session/cancel","params":{}})");
    bool response_processed = false;
    {
      std::unique_lock lock(reader_barrier_mutex);
      response_processed = reader_barrier_cv.wait_for(lock, 2s, [&] { return reader_barrier_reached; });
    }
    bool const response_staged = collateral && collateral->completion.wait_for(0ms) != std::future_status::ready;

    bool trigger_started = deadline_triggered;
    if (!deadline_triggered && trigger)
      trigger_started = peer.cancel_pending_call(trigger->id, "prompt canceled after fail-stop delivery");

    bool const trigger_ready = trigger && trigger->completion.wait_for(3s) == std::future_status::ready;
    auto trigger_result = trigger_ready ? std::optional<CallResult>(trigger->completion.get()) : std::nullopt;
    bool const collateral_ready = collateral && collateral->completion.wait_for(3s) == std::future_status::ready;
    auto collateral_result = collateral_ready ? std::optional<CallResult>(collateral->completion.get()) : std::nullopt;
    auto rejected_mutation = peer.send_request("fs/write_text_file", std::string("{}"), 2s, OutboundCallPolicy::AbortConnectionIfDelivered);

    peer.shutdown();
    thread.join();
    bool const trigger_is_ambiguous = trigger_result && !trigger_result->has_value() && trigger_result->error().code == -32603 &&
                                      trigger_result->error().message.find("outcome is unknown") != std::string::npos;
    bool const collateral_failed = collateral_result && !collateral_result->has_value() && collateral_result->error().code == -32800 &&
                                   collateral_result->error().message.find("connection closed") != std::string::npos;
    expect(trigger_record && delivery_barrier_sent && delivery_barrier_record && writer_claimed && response_processed && response_staged && trigger_started &&
               trigger_is_ambiguous && collateral_failed && !rejected_mutation && state->canceled && state->cancel_calls > 0 && run_result.has_value(),
           deadline_triggered
               ? "a delivered fail-stop deadline atomically cancels a claimed call with a reader-staged success and rejects later mutations"
               : "canceling a delivered fail-stop call atomically cancels a claimed call with a reader-staged success and rejects later mutations");
  };

  run_case(false);
  run_case(true);
}

void test_acp_peer_writer_acknowledged_lifecycle()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  state->block_writes = true;
  std::atomic_int side_effects = 0;
  std::atomic_bool completed = false;
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [&side_effects, &completed](Request const&, std::stop_token) -> RequestResult {
    side_effects.fetch_add(1, std::memory_order_relaxed);
    completed.store(true, std::memory_order_release);
    return std::string(R"({"ok":true})");
  });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);

  expect(peer.send_notification("test/block", std::string("{}")).has_value(), "ACP lifecycle test claims a leading stalled write");
  wait_writer(state);
  auto timed = peer.send_request("client/timeout-before-claim", std::string("{}"), 100ms);
  expect(timed.has_value(), "ACP lifecycle test queues an outbound call behind the stalled writer");
  if (timed)
  {
    auto premature = encode_success(timed->id, R"({"outcome":{"outcome":"selected","optionId":"allow_always"}})");
    feed(state, *premature);
    std::this_thread::sleep_for(20ms);
    bool const premature_fulfilled = timed->completion.wait_for(0ms) == std::future_status::ready;
    auto result = timed->completion.get();
    expect(!premature_fulfilled && !result && result.error().code == -32800,
           "a response for a queued outbound id is ignored and cannot grant permission before request delivery");
  }

  feed(state, R"({"jsonrpc":"2.0","id":"held","method":"work","params":{}})");
  while (!completed.load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
  feed(state, R"({"jsonrpc":"2.0","id":"held","method":"work","params":{}})");
  feed(state, R"({"jsonrpc":"2.0","method":"$/cancel_request","params":{"requestId":"held"}})");
  while (peer.stats().duplicate_inbound_ids == 0) std::this_thread::sleep_for(1ms);

  {
    std::lock_guard lock(state->mutex);
    state->block_writes = false;
    state->cv.notify_all();
  }
  auto blocker = take_output(state);
  auto first_response = take_output(state);
  auto duplicate_response = take_output(state);
  expect(blocker && blocker->find("test/block") != std::string::npos && first_response && first_response->find("\"result\"") != std::string::npos &&
             duplicate_response && side_effects.load() == 1 && peer.stats().canceled_inbound_requests == 0,
         "late cancellation loses to the atomically committed handler result while retaining the inbound id and preventing duplicate side effects");
  expect(!take_output(state, 80ms), "each admitted inbound envelope produces exactly one terminal response under a stalled writer");
  std::string const delivered = blocker.value_or("") + first_response.value_or("") + duplicate_response.value_or("");
  expect(!timed || delivered.find(id_debug_string(timed->id)) == std::string::npos,
         "a timed-out outbound call is skipped rather than sent after the writer resumes");

  close_input(state);
  thread.join();
  expect(run_result.has_value(), "writer acknowledgement lifecycle shuts down cleanly");

  auto eof_state = std::make_shared<MemoryTransportState>();
  eof_state->block_writes = true;
  JsonRpcPeer eof_peer(std::make_unique<MemoryTransport>(eof_state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  std::jthread eof_thread([&] { static_cast<void>(eof_peer.run()); });
  wait_reader(eof_state);
  static_cast<void>(eof_peer.send_notification("test/eof-block", std::string("{}")));
  wait_writer(eof_state);
  auto eof_call = eof_peer.send_request("client/eof-before-claim", std::string("{}"), 5s);
  close_input(eof_state);
  expect(eof_call.has_value() && !eof_call->completion.get(), "EOF releases an outbound call queued behind a claimed write");
  eof_thread.join();
  {
    std::lock_guard lock(eof_state->mutex);
    eof_state->block_writes = false;
    eof_state->cv.notify_all();
  }
  expect(eof_state->canceled && !take_output(eof_state, 80ms), "EOF aborts the claimed transport write and skips queued outbound records");
}

void test_acp_peer_claimed_outbound_abort_and_delivery_races()
{
  using namespace ava::app::acp;

  auto timeout_state = std::make_shared<MemoryTransportState>();
  timeout_state->block_writes = true;
  JsonRpcPeer timeout_peer(std::make_unique<MemoryTransport>(timeout_state),
                           [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult timeout_run;
  std::jthread timeout_thread([&] { timeout_run = timeout_peer.run(); });
  wait_reader(timeout_state);
  auto timed = timeout_peer.send_request("client/claimed-timeout", std::string("{}"), 250ms);
  wait_writer(timeout_state);
  bool const timed_ready = timed && timed->completion.wait_for(2s) == std::future_status::ready;
  auto timed_result = timed_ready ? std::optional<CallResult>(timed->completion.get()) : std::nullopt;
  timeout_thread.join();
  {
    std::lock_guard lock(timeout_state->mutex);
    timeout_state->block_writes = false;
    timeout_state->cv.notify_all();
  }
  expect(timed_result && !*timed_result && timed_result->error().code == -32800 && timeout_run.has_value() && timeout_state->canceled &&
             timeout_state->cancel_calls > 0 && timeout_state->write_attempts == 1 && !take_output(timeout_state, 80ms),
         "a claimed request timeout aborts the transport before bounded completion and never resumes the record");

  auto eof_state = std::make_shared<MemoryTransportState>();
  eof_state->block_writes = true;
  JsonRpcPeer eof_peer(std::make_unique<MemoryTransport>(eof_state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult eof_run;
  std::jthread eof_thread([&] { eof_run = eof_peer.run(); });
  wait_reader(eof_state);
  auto eof_call = eof_peer.send_request("client/claimed-eof", std::string("{}"), 5s);
  wait_writer(eof_state);
  close_input(eof_state);
  bool const eof_ready = eof_call && eof_call->completion.wait_for(2s) == std::future_status::ready;
  auto eof_result = eof_ready ? std::optional<CallResult>(eof_call->completion.get()) : std::nullopt;
  eof_thread.join();
  expect(eof_result && !*eof_result && eof_result->error().code == -32800 && eof_run.has_value() && eof_state->canceled && eof_state->cancel_calls > 0 &&
             eof_state->write_attempts == 1 && !take_output(eof_state, 80ms),
         "EOF aborts a request that is itself the claimed stalled record and completes peer and future without draining it");

  auto cancel_state = std::make_shared<MemoryTransportState>();
  cancel_state->block_writes = true;
  JsonRpcPeer cancel_peer(std::make_unique<MemoryTransport>(cancel_state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult cancel_run;
  std::jthread cancel_thread([&] { cancel_run = cancel_peer.run(); });
  wait_reader(cancel_state);
  auto canceled = cancel_peer.send_request("client/claimed-cancel", std::string("{}"), 5s);
  wait_writer(cancel_state);
  if (canceled)
    cancel_peer.cancel_pending_call(canceled->id, "prompt canceled during claimed permission write");
  bool const cancel_ready = canceled && canceled->completion.wait_for(2s) == std::future_status::ready;
  auto cancel_result = cancel_ready ? std::optional<CallResult>(canceled->completion.get()) : std::nullopt;
  cancel_thread.join();
  expect(cancel_result && !*cancel_result && cancel_result->error().code == -32800 && cancel_run.has_value() && cancel_state->canceled &&
             cancel_state->cancel_calls > 0 && cancel_state->write_attempts == 1 && !take_output(cancel_state, 80ms),
         "cancel_pending_call aborts a claimed request before bounded completion and does not resume it");

  auto staged_state = std::make_shared<MemoryTransportState>();
  staged_state->block_writes = true;
  JsonRpcPeer staged_peer(std::make_unique<MemoryTransport>(staged_state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  std::jthread staged_thread([&] { static_cast<void>(staged_peer.run()); });
  wait_reader(staged_state);
  auto staged = staged_peer.send_request("session/request_permission", std::string("{}"), 2s);
  wait_writer(staged_state);
  bool randomized_id = false;
  if (staged)
  {
    auto const* text_id = std::get_if<std::string>(&staged->id);
    randomized_id = text_id && *text_id != "ava-acp-1" && text_id->starts_with("ava-acp-connection_");
    auto guessed = encode_success(staged->id, R"({"outcome":{"outcome":"selected","optionId":"allow_always"}})");
    feed(staged_state, *guessed);
  }
  std::this_thread::sleep_for(20ms);
  bool const fulfilled_before_visible = staged && staged->completion.wait_for(0ms) == std::future_status::ready;
  bool output_before_release = false;
  {
    std::lock_guard lock(staged_state->mutex);
    output_before_release = !staged_state->output.empty();
    staged_state->block_writes = false;
    staged_state->cv.notify_all();
  }
  auto staged_record = take_output(staged_state);
  bool const staged_ready = staged && staged->completion.wait_for(2s) == std::future_status::ready;
  auto staged_result = staged_ready ? std::optional<CallResult>(staged->completion.get()) : std::nullopt;
  close_input(staged_state);
  staged_thread.join();
  expect(randomized_id && !fulfilled_before_visible && !output_before_release && staged_record && staged_result && staged_result->has_value(),
         "a guessed response while the permission request is writer-claimed is staged until delivery acknowledgement and randomized ids are non-predictable");

  auto delivered_state = std::make_shared<MemoryTransportState>();
  delivered_state->block_writes = true;
  JsonRpcPeer delivered_peer(std::make_unique<MemoryTransport>(delivered_state),
                             [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  std::jthread delivered_thread([&] { static_cast<void>(delivered_peer.run()); });
  wait_reader(delivered_state);
  auto delivered = delivered_peer.send_request("client/delivery-wins", std::string("{}"), 2s);
  wait_writer(delivered_state);
  {
    std::lock_guard lock(delivered_state->mutex);
    delivered_state->block_writes = false;
    delivered_state->cv.notify_all();
  }
  auto delivered_record = take_output(delivered_state);
  if (delivered)
  {
    auto response = encode_success(delivered->id, R"({"winner":"delivery"})");
    feed(delivered_state, *response);
  }
  bool const delivered_ready = delivered && delivered->completion.wait_for(2s) == std::future_status::ready;
  auto delivered_result = delivered_ready ? std::optional<CallResult>(delivered->completion.get()) : std::nullopt;
  bool canceled_before_close = false;
  {
    std::lock_guard lock(delivered_state->mutex);
    canceled_before_close = delivered_state->canceled;
  }
  close_input(delivered_state);
  delivered_thread.join();
  expect(delivered_record && delivered_result && delivered_result->has_value() && !canceled_before_close,
         "writer delivery acknowledgment and response win the mutex race with normal pending-call semantics");

  auto ambiguous_state = std::make_shared<MemoryTransportState>();
  ambiguous_state->block_writes = true;
  ambiguous_state->publish_before_stall = true;
  JsonRpcPeer ambiguous_peer(std::make_unique<MemoryTransport>(ambiguous_state),
                             [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult ambiguous_run;
  std::jthread ambiguous_thread([&] { ambiguous_run = ambiguous_peer.run(); });
  wait_reader(ambiguous_state);
  auto ambiguous = ambiguous_peer.send_request("client/ambiguous-race", std::string("{}"), 250ms);
  wait_writer(ambiguous_state);
  bool const ambiguous_ready = ambiguous && ambiguous->completion.wait_for(2s) == std::future_status::ready;
  auto ambiguous_result = ambiguous_ready ? std::optional<CallResult>(ambiguous->completion.get()) : std::nullopt;
  ambiguous_thread.join();
  auto physically_delivered = take_output(ambiguous_state);
  expect(ambiguous_result && !*ambiguous_result && ambiguous_result->error().code == -32603 &&
             ambiguous_result->error().message.find("outcome is unknown") != std::string::npos && ambiguous_run.has_value() && ambiguous_state->canceled &&
             physically_delivered && !take_output(ambiguous_state, 80ms),
         "a full physical delivery reported after cancellation yields one ambiguous-delivery winner and no resumed duplicate record");
}

void test_acp_prompt_admission_rollback_and_control_cancel_saturation()
{
  using namespace ava::app::acp;

  auto run_case = [](bool cancel_with_json_rpc) {
    auto root = std::filesystem::temp_directory_path() / ava::core::make_id("acp-admission-order");
    auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    configure_acp_test_model(root);
    std::string body;
    AgentServiceOptions options;
    options.agent_version = "1";
    options.launch_root = std::filesystem::canonical(workspace);
    options.paths = ava::tests::app_test_paths(root);
    options.provider_bundle_factory = recording_bundle_factory(&body);
    AgentService service(options);
    static_cast<void>(service.handle_request(initialize_request(), {}));
    auto created = service.handle_request(
        Request{.id = std::int64_t(2), .method = "session/new", .params_json = std::string("{\"cwd\":\"") + workspace.string() + "\",\"mcpServers\":[]}"}, {});
    auto session_id = created ? ava::core::json::string_field(*created, "sessionId") : std::nullopt;
    expect(session_id.has_value(), "ACP admission-order test creates a session");
    if (!session_id)
      return;

    auto state = std::make_shared<MemoryTransportState>();
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    std::size_t blockers_entered = 0;
    bool release = false;
    std::atomic_int control_cancels = 0;
    JsonRpcPeer peer(
        std::make_unique<MemoryTransport>(state), [&service](Request const& request, std::stop_token token) { return service.handle_request(request, token); },
        [&gate_mutex, &gate_cv, &blockers_entered, &release](Notification const&, std::stop_token) {
          std::unique_lock lock(gate_mutex);
          ++blockers_entered;
          gate_cv.notify_all();
          gate_cv.wait(lock, [&release] { return release; });
        });
    peer.set_request_pre_admission_hook([&service](Request const& request) { return service.pre_admit_request(request); });
    peer.set_control_notification_handler([&service, &control_cancels](Notification const& notification) {
      control_cancels.fetch_add(1, std::memory_order_relaxed);
      service.handle_control_notification(notification);
    });
    service.bind_update_sender([&peer](std::string_view id, std::string_view update) -> ava::core::VoidResult {
      return peer.send_notification("session/update",
                                    std::string("{\"sessionId\":\"") + ava::core::json::escape(id) + "\",\"update\":" + std::string(update) + "}");
    });

    ava::core::VoidResult run_result;
    std::jthread peer_thread([&] { run_result = peer.run(); });
    wait_reader(state);
    for (std::size_t index = 0; index < kWorkerCount; ++index) feed(state, R"({"jsonrpc":"2.0","method":"test/block","params":{}})");
    {
      std::unique_lock lock(gate_mutex);
      static_cast<void>(gate_cv.wait_for(lock, 2s, [&] { return blockers_entered == kWorkerCount; }));
    }

    auto prompt_record = std::string("{\"jsonrpc\":\"2.0\",\"id\":\"queued\",\"method\":\"session/prompt\",\"params\":{\"sessionId\":\"") + *session_id +
                         "\",\"prompt\":[{\"type\":\"text\",\"text\":\"queued\"}]}}";
    feed(state, prompt_record);
    std::this_thread::sleep_for(20ms);
    if (cancel_with_json_rpc)
    {
      feed(state, R"({"jsonrpc":"2.0","method":"$/cancel_request","params":{"requestId":"queued"}})");
      while (peer.stats().canceled_inbound_requests == 0) std::this_thread::sleep_for(1ms);
    }
    else
    {
      for (std::size_t index = 1; index < kMaxWorkerQueue; ++index) feed(state, R"({"jsonrpc":"2.0","method":"test/queued","params":{}})");
      for (int index = 0; index < 8; ++index) feed(state, R"({"jsonrpc":"2.0","method":"test/dropped","params":{}})");
      feed(state, std::string("{\"jsonrpc\":\"2.0\",\"method\":\"session/cancel\",\"params\":{\"sessionId\":\"") + *session_id + "\"}}");
      while (control_cancels.load(std::memory_order_acquire) == 0) std::this_thread::sleep_for(1ms);
    }
    {
      std::lock_guard lock(gate_mutex);
      release = true;
    }
    gate_cv.notify_all();

    auto canceled = take_output(state);
    bool const cancellation_ok =
        cancel_with_json_rpc ? output_has_code(canceled, -32800) : canceled && canceled->find("\"stopReason\":\"cancelled\"") != std::string::npos;
    expect(cancellation_ok, cancel_with_json_rpc ? "queued prompt $/cancel_request rolls back pre-admission before handler consumption"
                                                 : "reader-ordered session/cancel remains non-droppable under a saturated generic notification queue");
    if (!cancel_with_json_rpc)
      expect(peer.stats().dropped_notifications > 0 && control_cancels.load(std::memory_order_relaxed) == 1,
             "generic notifications saturate and drop while the bounded session/cancel control path executes exactly once");

    feed(state, std::string("{\"jsonrpc\":\"2.0\",\"id\":\"later\",\"method\":\"session/prompt\",\"params\":{\"sessionId\":\"") + *session_id +
                    "\",\"prompt\":[{\"type\":\"text\",\"text\":\"later succeeds\"}]}}");
    std::optional<std::string> later;
    for (int index = 0; index < 4 && !later; ++index)
    {
      auto record = take_output(state);
      if (record && record->find("\"id\":\"later\"") != std::string::npos)
        later = std::move(record);
    }
    expect(later && later->find("\"stopReason\":\"end_turn\"") != std::string::npos, "a later prompt succeeds after canceled queued admission rollback");

    feed(state, std::string("{\"jsonrpc\":\"2.0\",\"id\":\"close\",\"method\":\"session/close\",\"params\":{\"sessionId\":\"") + *session_id + "\"}}");
    auto closed = take_output(state);
    expect(closed && closed->find("\"id\":\"close\"") != std::string::npos && closed->find("\"result\":{}") != std::string::npos,
           "session close succeeds without a leaked prompt reservation");

    close_input(state);
    peer_thread.join();
    service.unbind_update_sender();
    service.shutdown();
    expect(run_result.has_value(), "admission and control cancellation test shuts down cleanly");
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
  };

  run_case(true);
  run_case(false);
}

void test_acp_peer_shutdown_abandons_queued_unstarted_request()
{
  using namespace ava::app::acp;

  auto state = std::make_shared<MemoryTransportState>();
  std::mutex blockers_mutex;
  std::condition_variable blockers_cv;
  std::size_t blockers_entered = 0;
  std::size_t blockers_stopped = 0;
  std::atomic_int target_executions = 0;
  std::atomic_int admissions = 0;
  std::atomic_int rollbacks = 0;
  std::atomic_int escalation_calls = 0;
  JsonRpcPeer peer(
      std::make_unique<MemoryTransport>(state),
      [&target_executions](Request const&, std::stop_token) -> RequestResult {
        target_executions.fetch_add(1, std::memory_order_relaxed);
        return std::string("{}");
      },
      [&blockers_mutex, &blockers_cv, &blockers_entered, &blockers_stopped](Notification const&, std::stop_token token) {
        std::unique_lock lock(blockers_mutex);
        ++blockers_entered;
        blockers_cv.notify_all();
        std::stop_callback notify_stop(token, [&blockers_cv] { blockers_cv.notify_all(); });
        blockers_cv.wait(lock, [&token] { return token.stop_requested(); });
        ++blockers_stopped;
        blockers_cv.notify_all();
      },
      {}, std::make_unique<RecordingShutdownEscalation>(escalation_calls));
  peer.set_request_pre_admission_hook([&admissions, &rollbacks, &blockers_cv](Request const&) -> std::expected<std::function<void()>, JsonRpcError> {
    admissions.fetch_add(1, std::memory_order_release);
    blockers_cv.notify_all();
    return std::function<void()>([&rollbacks] { rollbacks.fetch_add(1, std::memory_order_relaxed); });
  });

  ava::core::VoidResult run_result;
  std::jthread peer_thread([&] { run_result = peer.run(); });
  wait_reader(state);
  for (std::size_t index = 0; index < kWorkerCount; ++index) feed(state, R"({"jsonrpc":"2.0","method":"test/block-worker","params":{}})");

  bool all_blockers_entered = false;
  {
    std::unique_lock lock(blockers_mutex);
    all_blockers_entered = blockers_cv.wait_for(lock, 2s, [&] { return blockers_entered == kWorkerCount; });
  }
  expect(all_blockers_entered, "ACP queued-abandonment test occupies every peer worker before admitting its target request");
  if (!all_blockers_entered)
  {
    close_input(state);
    peer_thread.join();
    return;
  }

  feed(state, R"({"jsonrpc":"2.0","id":1,"method":"test/queued-target","params":{}})");
  bool target_admitted = false;
  {
    std::unique_lock lock(blockers_mutex);
    target_admitted = blockers_cv.wait_for(lock, 2s, [&] { return admissions.load(std::memory_order_acquire) == 1; });
  }
  expect(target_admitted, "ACP queued-abandonment test observes target pre-admission before EOF");
  close_input(state);
  peer_thread.join();

  expect(target_admitted && target_executions.load(std::memory_order_relaxed) == 0 && rollbacks.load(std::memory_order_relaxed) == 1,
         "EOF abandons an admitted but unstarted request and rolls back its reservation exactly once");
  expect(blockers_stopped == kWorkerCount, "EOF stops every cooperative worker blocker");
  expect(run_result.has_value() && escalation_calls.load(std::memory_order_relaxed) == 0,
         "queued-unstarted abandonment exits the peer cleanly without shutdown escalation");
}

void test_acp_peer_started_non_cooperative_shutdown_escalates()
{
  using namespace ava::app::acp;

  int entry_pipe[2] = {-1, -1};
  bool const pipe_created = pipe(entry_pipe) == 0;
  expect(pipe_created, "ACP escalation regression creates a handler-entry signal pipe");
  if (!pipe_created)
    return;

  pid_t const child = fork();
  expect(child >= 0, "ACP escalation regression forks a child process");
  if (child == 0)
  {
    static_cast<void>(close(entry_pipe[0]));
    auto state = std::make_shared<MemoryTransportState>();
    JsonRpcPeer peer(
        std::make_unique<MemoryTransport>(state),
        [state, entry_fd = entry_pipe[1]](Request const&, std::stop_token) -> RequestResult {
          char const entered = '1';
          if (write(entry_fd, &entered, 1) != 1)
            std::_Exit(98);
          static_cast<void>(close(entry_fd));
          close_input(state);
          while (true) std::this_thread::sleep_for(10ms);
        },
        {}, {}, make_process_shutdown_escalation(), 100ms);
    std::jthread request_feeder([state] {
      wait_reader(state);
      feed(state, R"({"jsonrpc":"2.0","id":1,"method":"never","params":{}})");
    });
    static_cast<void>(peer.run());
    std::_Exit(99);
  }

  static_cast<void>(close(entry_pipe[1]));
  if (child < 0)
  {
    static_cast<void>(close(entry_pipe[0]));
    return;
  }

  int status = 0;
  bool exited = false;
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto const waited = waitpid(child, &status, WNOHANG);
    if (waited == child)
    {
      exited = true;
      break;
    }
    std::this_thread::sleep_for(10ms);
  }
  if (!exited)
  {
    static_cast<void>(kill(child, SIGKILL));
    static_cast<void>(waitpid(child, &status, 0));
  }

  char entry_signal = '\0';
  auto const signal_bytes = read(entry_pipe[0], &entry_signal, 1);
  static_cast<void>(close(entry_pipe[0]));
  expect(signal_bytes == 1 && entry_signal == '1', "the non-cooperative request handler starts before causing transport EOF");
  expect(exited && WIFEXITED(status) && WEXITSTATUS(status) == kShutdownEscalationExitCode,
         "a started non-cooperative request causes bounded normal shutdown escalation exit 70");
}

void test_acp_peer_write_failure_wakes_reader_and_shutdown_race()
{
  using namespace ava::app::acp;
  auto state = std::make_shared<MemoryTransportState>();
  state->fail_writes = true;
  JsonRpcPeer peer(std::make_unique<MemoryTransport>(state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); });
  ava::core::VoidResult run_result;
  std::jthread thread([&] { run_result = peer.run(); });
  wait_reader(state);
  feed(state, R"({"jsonrpc":"2.0","id":1,"method":"x","params":{}})");
  thread.join();
  expect(!run_result, "ACP write failure wakes the reader and terminates the connection");

  auto blocked_state = std::make_shared<MemoryTransportState>();
  std::atomic_bool entered = false;
  JsonRpcPeer blocked(std::make_unique<MemoryTransport>(blocked_state), [&entered](Request const&, std::stop_token token) -> RequestResult {
    entered.store(true, std::memory_order_release);
    while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
    return std::string("{}");
  });
  ava::core::VoidResult blocked_result;
  std::jthread blocked_thread([&] { blocked_result = blocked.run(); });
  wait_reader(blocked_state);
  feed(blocked_state, R"({"jsonrpc":"2.0","id":1,"method":"blocked","params":{}})");
  while (!entered.load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
  blocked.shutdown();
  blocked.shutdown();
  blocked_thread.join();
  expect(blocked_result.has_value(), "ACP idempotent shutdown cancels and joins a cooperatively blocked request handler");

  auto notification_state = std::make_shared<MemoryTransportState>();
  std::atomic_bool notification_entered = false;
  std::atomic_bool notification_stopped = false;
  JsonRpcPeer notifications(
      std::make_unique<MemoryTransport>(notification_state), [](Request const&, std::stop_token) -> RequestResult { return std::string("{}"); },
      [&notification_entered, &notification_stopped](Notification const&, std::stop_token token) {
        notification_entered.store(true, std::memory_order_release);
        while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
        notification_stopped.store(true, std::memory_order_release);
      });
  std::jthread notification_thread([&] { static_cast<void>(notifications.run()); });
  wait_reader(notification_state);
  feed(notification_state, R"({"jsonrpc":"2.0","method":"blocked-notification","params":{}})");
  while (!notification_entered.load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
  notifications.shutdown();
  notification_thread.join();
  expect(notification_stopped.load(std::memory_order_acquire), "ACP notification handlers receive connection shutdown cancellation");
}

}  // namespace

void run_acp_tests()
{
  test_acp_prompt_content_capabilities_and_strict_validation();
  test_acp_typed_session_update_mapper_ordering_and_limits();
  test_acp_permission_dtos_show_bounded_actions();
  test_acp_codec_envelopes_and_ids();
  test_acp_codec_initialize_meta_additive_and_malformed_fields();
  test_acp_codec_utf8_depth_size_and_serialization();
  test_acp_transport_lf_crlf_and_final_record();
  test_acp_service_gating_reinitialize_and_negotiation();
  test_acp_service_mutating_request_terminal_commits();
  test_acp_session_request_schema_defaults_and_invalid_item_skipping();
  test_acp_session_capacity_is_reserved_before_persistence();
  test_acp_startup_model_is_pinned_across_config_mutation();
  test_acp_resume_validates_history_against_pinned_startup_model();
  test_acp_session_lifecycle_real_prompt_and_provider_ownership();
  test_acp_exact_identity_persisted_cwd_and_restart();
  test_acp_cross_process_lease_and_bounded_streaming();
  test_acp_list_pagination_cancel_race_stop_reasons_and_file_safety();
  test_acp_cancel_terminal_arbitration_and_provider_setup_paths();
  test_acp_session_mcp_requires_persistent_operator_authorization();
  test_acp_strict_session_mcp_registry_and_error_propagation();
  test_acp_negotiated_client_filesystem_and_terminal_routing();
  test_acp_builtin_permission_gateway_one_shot_mutations_and_updates();
  test_acp_session_grant_cannot_follow_retargeted_parent_symlink();
  test_acp_permission_once_always_reject_cancel_invalid_and_hard_policy_matrix();
  test_acp_close_timeout_is_internal_error_with_eventual_cleanup();
  test_acp_peer_prompt_terminal_commit_arbitration();
  test_acp_client_tool_dtos_lifecycle_and_cancellation();
  test_acp_peer_lifecycle_notifications_and_duplicate_ids();
  test_acp_peer_bidirectional_out_of_order_deadline_and_late_response();
  test_acp_peer_cancel_duplicate_inflight_and_saturation();
  test_acp_peer_lifecycle_request_commit_linearization();
  test_acp_peer_outbound_queue_saturation();
  test_acp_peer_delivered_fail_stop_cancellation();
  test_acp_peer_fail_stop_poison_arbitrates_all_pending_calls();
  test_acp_peer_writer_acknowledged_lifecycle();
  test_acp_peer_claimed_outbound_abort_and_delivery_races();
  test_acp_prompt_admission_rollback_and_control_cancel_saturation();
  test_acp_peer_shutdown_abandons_queued_unstarted_request();
  test_acp_peer_started_non_cooperative_shutdown_escalates();
  test_acp_peer_write_failure_wakes_reader_and_shutdown_race();
}
