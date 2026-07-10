#pragma once

#include "ava/app/headless_policy.h"
#include "ava/app/runtime.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

namespace ava::app {

enum class PrintOutputFormat
{
  Text,
  Json,
};

struct PrintPromptInputs
{
  std::optional<std::string> explicit_prompt;
  std::optional<std::string> stdin_prompt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PrintModeRunOptions
{
  PrintOutputFormat output_format = PrintOutputFormat::Text;
  RuntimeRunOptions runtime_options;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PrintModeOptions
{
  RuntimeOpenOptions open_options;
  std::optional<std::string> explicit_prompt;
  bool read_stdin = false;
  PrintOutputFormat output_format = PrintOutputFormat::Text;
  HeadlessPermissionPolicyOptions permission_policy;
  std::optional<std::reference_wrapper<ava::provider::Provider const>> provider_override;
  std::optional<std::reference_wrapper<ava::provider::Transport>> transport_override;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<std::string> merge_print_prompt(PrintPromptInputs const& inputs);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_print_prompt(RuntimeSession& session, std::string const& prompt,
                                                                              ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                              PrintModeRunOptions const& options, std::ostream& out, std::ostream& err);

[[nodiscard]] int run_print_mode(PrintModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace ava::app
