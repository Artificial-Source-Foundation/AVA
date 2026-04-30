#pragma once

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

#include "ava/app/headless_policy.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::app {

enum class PrintOutputFormat {
  Text,
  Json,
};

struct PrintPromptInputs {
  std::optional<std::string> explicit_prompt;
  std::optional<std::string> stdin_prompt;
};

struct PrintModeRunOptions {
  PrintOutputFormat output_format = PrintOutputFormat::Text;
  RuntimeRunOptions runtime_options;
};

struct PrintModeOptions {
  RuntimeOpenOptions open_options;
  std::optional<std::string> explicit_prompt;
  bool read_stdin = false;
  PrintOutputFormat output_format = PrintOutputFormat::Text;
  HeadlessPermissionPolicyOptions permission_policy;
  std::optional<std::reference_wrapper<const ava::provider::Provider>> provider_override;
  std::optional<std::reference_wrapper<ava::provider::Transport>> transport_override;
};

[[nodiscard]] ava::core::Result<std::string> merge_print_prompt(const PrintPromptInputs& inputs);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_print_prompt(
    RuntimeSession& session, const std::string& prompt, const ava::provider::Provider& provider,
    ava::provider::Transport& transport, const PrintModeRunOptions& options, std::ostream& out, std::ostream& err);

[[nodiscard]] int run_print_mode(const PrintModeOptions& options, std::istream& in, std::ostream& out,
                                 std::ostream& err);

}  // namespace ava::app
