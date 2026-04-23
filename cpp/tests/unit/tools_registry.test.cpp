#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/tools/permission_middleware.hpp"
#include "ava/tools/retry.hpp"
#include "ava/tools/registry.hpp"

namespace {

class EchoTool final : public ava::tools::Tool {
 public:
  [[nodiscard]] std::string name() const override { return "echo"; }
  [[nodiscard]] std::string description() const override { return "Echo tool"; }
  [[nodiscard]] nlohmann::json parameters() const override {
    return nlohmann::json{{"type", "object"}};
  }

  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override {
    return ava::types::ToolResult{
        .call_id = "tool-local-id",
        .content = args.value("input", std::string("")),
        .is_error = false,
    };
  }
};

class RecordingMiddleware final : public ava::tools::Middleware {
 public:
  explicit RecordingMiddleware(std::vector<std::string>* events) : events_(events) {}

  void before(const ava::types::ToolCall& tool_call) const override {
    events_->push_back("before:" + tool_call.name);
  }

  [[nodiscard]] ava::types::ToolResult after(
      const ava::types::ToolCall& tool_call,
      const ava::types::ToolResult& result
  ) const override {
    events_->push_back("after:" + tool_call.name);
    auto wrapped = result;
    wrapped.content += ":wrapped";
    return wrapped;
  }

 private:
  std::vector<std::string>* events_;
};

class FlakyReadTool final : public ava::tools::Tool {
 public:
  explicit FlakyReadTool(std::size_t fail_times) : fail_times_(fail_times) {}

  [[nodiscard]] std::string name() const override { return "read"; }
  [[nodiscard]] std::string description() const override { return "flaky read"; }
  [[nodiscard]] nlohmann::json parameters() const override {
    return nlohmann::json{{"type", "object"}};
  }

  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override {
    (void)args;
    ++attempts_;
    if(attempts_ <= fail_times_) {
      throw std::runtime_error("connection refused");
    }
    return ava::types::ToolResult{.call_id = "", .content = "ok", .is_error = false};
  }

  mutable std::size_t attempts_{0};

 private:
  std::size_t fail_times_;
};

class AskInspector final : public ava::tools::PermissionInspector {
 public:
  [[nodiscard]] ava::tools::PermissionInspection inspect(
      const std::string& tool_name,
      const nlohmann::json& arguments
  ) const override {
    (void)arguments;
    return ava::tools::PermissionInspection{
        .action = ava::tools::PermissionAction::Ask,
        .reason = "approval needed for " + tool_name,
        .risk_level = "medium",
    };
  }
};

class AlwaysAllowBridge final : public ava::tools::ApprovalBridge {
 public:
  [[nodiscard]] ava::tools::ToolApproval request_approval(
      const ava::types::ToolCall& call,
      const ava::tools::PermissionInspection& inspection
  ) const override {
    (void)call;
    (void)inspection;
    return ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed};
  }
};

}  // namespace

TEST_CASE("tool registry executes tool and normalizes call_id", "[ava_tools]") {
  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<EchoTool>());

  const auto result = registry.execute(ava::types::ToolCall{
      .id = "call_123",
      .name = "echo",
      .arguments = nlohmann::json{{"input", "hello"}},
  });

  REQUIRE(result.call_id == "call_123");
  REQUIRE(result.content == "hello");
}

TEST_CASE("tool registry runs middleware before and after execution", "[ava_tools]") {
  std::vector<std::string> events;

  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<EchoTool>());
  registry.add_middleware(std::make_shared<RecordingMiddleware>(&events));

  const auto result = registry.execute(ava::types::ToolCall{
      .id = "call_1",
      .name = "echo",
      .arguments = nlohmann::json{{"input", "hello"}},
  });

  REQUIRE(result.content == "hello:wrapped");
  REQUIRE(events == std::vector<std::string>{"before:echo", "after:echo"});
}

TEST_CASE("retry helpers follow milestone 6 behavior", "[ava_tools]") {
  REQUIRE(ava::tools::retry::MAX_RETRIES == 2);
  REQUIRE(ava::tools::retry::is_retryable_tool("read"));
  REQUIRE_FALSE(ava::tools::retry::is_retryable_tool("write"));
  REQUIRE(ava::tools::retry::is_transient_error("permission denied"));
  REQUIRE_FALSE(ava::tools::retry::is_transient_error("file not found"));
  REQUIRE(ava::tools::retry::backoff_for_attempt(0).value().count() == 100);
  REQUIRE(ava::tools::retry::backoff_for_attempt(1).value().count() == 200);
  REQUIRE_FALSE(ava::tools::retry::backoff_for_attempt(2).has_value());
}

TEST_CASE("registry retries transient failures for retryable tools", "[ava_tools]") {
  auto flaky = std::make_unique<FlakyReadTool>(1);
  auto* flaky_ptr = flaky.get();

  ava::tools::ToolRegistry registry;
  registry.register_tool(std::move(flaky));

  const auto result = registry.execute(ava::types::ToolCall{
      .id = "call_1",
      .name = "read",
      .arguments = nlohmann::json::object(),
  });

  REQUIRE(result.content == "ok");
  REQUIRE(flaky_ptr->attempts_ == 2);
}

TEST_CASE("permission middleware fails closed when approval bridge is missing", "[ava_tools]") {
  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<EchoTool>());
  registry.add_middleware(std::make_shared<ava::tools::PermissionMiddleware>(std::make_shared<AskInspector>()));

  REQUIRE_THROWS_WITH(
      registry.execute(ava::types::ToolCall{
          .id = "call_1",
          .name = "echo",
          .arguments = nlohmann::json::object(),
      }),
      Catch::Matchers::ContainsSubstring("requires approval")
  );
}

TEST_CASE("permission middleware allows execution when bridge approves", "[ava_tools]") {
  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<EchoTool>());
  registry.add_middleware(std::make_shared<ava::tools::PermissionMiddleware>(
      std::make_shared<AskInspector>(),
      std::make_shared<AlwaysAllowBridge>()
  ));

  const auto result = registry.execute(ava::types::ToolCall{
      .id = "call_1",
      .name = "echo",
      .arguments = nlohmann::json{{"input", "approved"}},
  });

  REQUIRE(result.content == "approved");
}

TEST_CASE("permission middleware is safe under concurrent session approval", "[ava_tools]") {
  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<EchoTool>());
  registry.add_middleware(std::make_shared<ava::tools::PermissionMiddleware>(
      std::make_shared<AskInspector>(),
      std::make_shared<AlwaysAllowBridge>()
  ));

  std::vector<std::thread> threads;
  std::vector<std::string> outputs(8);
  std::mutex outputs_mutex;
  for(std::size_t idx = 0; idx < outputs.size(); ++idx) {
    threads.emplace_back([&, idx] {
      const auto result = registry.execute(ava::types::ToolCall{
          .id = "call_" + std::to_string(idx),
          .name = "echo",
          .arguments = nlohmann::json{{"input", "ok"}},
      });
      const std::lock_guard<std::mutex> lock(outputs_mutex);
      outputs[idx] = result.content;
    });
  }

  for(auto& thread : threads) {
    thread.join();
  }

  for(const auto& output : outputs) {
    REQUIRE(output == "ok");
  }
}
