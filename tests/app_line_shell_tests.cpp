#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/line_shell_internal.h"

#include <sstream>
#include <string>

namespace {

using ava::app::line_shell_internal::BoundedLineStatus;
using ava::app::line_shell_internal::kLineShellMaxSubmittedBytes;
using ava::app::line_shell_internal::LinePermissionChoice;
using ava::app::line_shell_internal::read_bounded_line;
using ava::app::line_shell_internal::resolve_line_permission_prompt;
using ava::app::line_shell_internal::resolve_line_question_prompt;

void test_line_shell_bounded_reader_accepts_limit_and_recovers_after_oversize()
{
  std::string line;
  std::istringstream at_limit(std::string(kLineShellMaxSubmittedBytes, 'x') + "\nnext\n");
  expect(read_bounded_line(at_limit, line, kLineShellMaxSubmittedBytes) == BoundedLineStatus::Line && line.size() == kLineShellMaxSubmittedBytes,
         "line shell accepts a submitted line at the 64 KiB limit");
  expect(read_bounded_line(at_limit, line, kLineShellMaxSubmittedBytes) == BoundedLineStatus::Line && line == "next",
         "line shell continues after an at-limit submitted line");

  std::istringstream over_limit(std::string(kLineShellMaxSubmittedBytes + 1, 'y') + "\nrecovered\n");
  expect(read_bounded_line(over_limit, line, kLineShellMaxSubmittedBytes) == BoundedLineStatus::TooLong && line.empty(),
         "line shell rejects and clears a submitted line over 64 KiB");
  expect(read_bounded_line(over_limit, line, kLineShellMaxSubmittedBytes) == BoundedLineStatus::Line && line == "recovered",
         "line shell drains an oversized line and reads the next complete line");

  std::istringstream final_line("without newline");
  expect(read_bounded_line(final_line, line, kLineShellMaxSubmittedBytes) == BoundedLineStatus::Line && line == "without newline" &&
             read_bounded_line(final_line, line, kLineShellMaxSubmittedBytes) == BoundedLineStatus::EndOfInput,
         "line shell accepts a bounded final unterminated line before reporting EOF");
}

ava::permissions::PermissionPrompt permission_prompt()
{
  ava::permissions::PermissionPrompt prompt;
  prompt.operation = ava::permissions::Operation::RunCommand;
  prompt.mode = ava::core::Mode::Build;
  prompt.tool_name = "bash";
  prompt.command = "printf safe";
  prompt.reason = std::string("hostile") + '\x1b' + "]8;;https://example.invalid";
  prompt.risk = ava::permissions::PermissionRisk::High;
  return prompt;
}

void test_line_shell_permission_numbered_text_cancel_and_guidance()
{
  auto const prompt = permission_prompt();
  std::string guidance;
  std::istringstream numbered("not-a-choice\n3\n");
  std::ostringstream numbered_output;
  auto const selected = resolve_line_permission_prompt(prompt, true, true, true, numbered, numbered_output, guidance);
  expect(selected == LinePermissionChoice::AllowSession && numbered_output.str().find("Invalid permission answer") != std::string::npos &&
             numbered_output.str().find("Allow for this session") != std::string::npos && numbered_output.str().find('\x1b') == std::string::npos &&
             numbered_output.str().find("hostile?]8;;https://example.invalid") != std::string::npos,
         "line-shell permission prompt retries invalid numbered input, exposes eligible scopes, and sanitizes fields");

  std::istringstream text_choice("always deny\n");
  std::ostringstream text_output;
  expect(resolve_line_permission_prompt(prompt, false, false, true, text_choice, text_output, guidance) == LinePermissionChoice::DenyRemember,
         "line-shell permission prompt accepts a textual persistent-deny choice");

  std::istringstream guided("guide\nstay in the workspace\n");
  std::ostringstream guided_output;
  expect(resolve_line_permission_prompt(prompt, false, false, false, guided, guided_output, guidance) == LinePermissionChoice::Deny &&
             guidance == "stay in the workspace",
         "line-shell permission prompt carries validated one-shot denial guidance");

  std::istringstream canceled("cancel\n");
  std::ostringstream canceled_output;
  expect(resolve_line_permission_prompt(prompt, false, false, false, canceled, canceled_output, guidance) == LinePermissionChoice::Cancel &&
             canceled_output.str().find("not allowed") != std::string::npos,
         "line-shell permission cancellation fails closed with plain-language output");

  std::istringstream eof;
  std::ostringstream eof_output;
  expect(resolve_line_permission_prompt(prompt, false, false, false, eof, eof_output, guidance) == LinePermissionChoice::Cancel &&
             eof_output.str().find("end of input") != std::string::npos,
         "line-shell permission EOF fails closed with plain-language output");
}

ava::agent::QuestionPrompt question_prompt(bool multiple = false)
{
  return ava::agent::QuestionPrompt{.header = std::string("Pick") + '\x1b' + "[31m",
                                    .question = "Continue?",
                                    .options = {{.value = "alpha", .label = "Alpha"}, {.value = "beta", .label = "Beta"}, {.value = "gamma", .label = "Gamma"}},
                                    .multiple = multiple,
                                    .allow_custom = true};
}

void test_line_shell_question_success_retry_multi_custom_and_cancel()
{
  std::istringstream single("9\n2\n");
  std::ostringstream single_output;
  auto single_answer = resolve_line_question_prompt(question_prompt(), single, single_output);
  expect(single_answer && single_answer->selected_options == std::vector<std::string>{"beta"} &&
             single_output.str().find("Invalid question answer") != std::string::npos && single_output.str().find('\x1b') == std::string::npos &&
             single_output.str().find("Question: Pick?[31m") != std::string::npos,
         "line-shell question prompt retries an invalid number, resolves a numbered answer, and sanitizes output");

  std::istringstream multi("1,1\n1, 3\n");
  std::ostringstream multi_output;
  auto multi_answer = resolve_line_question_prompt(question_prompt(true), multi, multi_output);
  expect(multi_answer && multi_answer->selected_options == (std::vector<std::string>{"alpha", "gamma"}) &&
             multi_output.str().find("unique comma-separated numbers") != std::string::npos,
         "line-shell multi-question rejects duplicate choices and retries with validated selections");

  std::istringstream custom("my custom answer\n");
  std::ostringstream custom_output;
  auto custom_answer = resolve_line_question_prompt(question_prompt(), custom, custom_output);
  expect(custom_answer && custom_answer->custom_text == "my custom answer", "line-shell question prompt accepts permitted custom text");

  std::istringstream canceled("c\n");
  std::ostringstream canceled_output;
  auto canceled_answer = resolve_line_question_prompt(question_prompt(), canceled, canceled_output);
  expect(!canceled_answer && canceled_answer.error().message() == "question prompt canceled" &&
             canceled_output.str().find("Question canceled") != std::string::npos,
         "line-shell question cancellation returns the existing cancellation contract");

  std::istringstream eof;
  std::ostringstream eof_output;
  auto eof_answer = resolve_line_question_prompt(question_prompt(), eof, eof_output);
  expect(!eof_answer && eof_answer.error().message() == "question prompt canceled" && eof_output.str().find("end of input") != std::string::npos,
         "line-shell question EOF returns the existing cancellation contract with plain-language output");
}

void test_line_shell_question_invalid_and_secret_fail_bounded()
{
  std::istringstream invalid("9\n8\n7\n");
  std::ostringstream invalid_output;
  auto invalid_answer = resolve_line_question_prompt(question_prompt(), invalid, invalid_output);
  expect(!invalid_answer && invalid_answer.error().message().find("too many invalid answers") != std::string::npos &&
             invalid_output.str().find("Too many invalid question answers") != std::string::npos,
         "line-shell question retries are finite and fail with an actionable cancellation");

  auto secret_prompt = question_prompt();
  secret_prompt.secret = true;
  std::istringstream secret_input("must-not-be-read\n");
  std::ostringstream secret_output;
  auto secret_answer = resolve_line_question_prompt(secret_prompt, secret_input, secret_output);
  std::string untouched;
  expect(!secret_answer && secret_answer.error().message().find("unavailable in --line-shell") != std::string::npos &&
             read_bounded_line(secret_input, untouched, 1024) == BoundedLineStatus::Line && untouched == "must-not-be-read" &&
             secret_output.str().find("ava login <provider> --api-key") != std::string::npos,
         "line shell never reads or echoes secret question input");
}

}  // namespace

void run_app_line_shell_tests()
{
  test_line_shell_bounded_reader_accepts_limit_and_recovers_after_oversize();
  test_line_shell_permission_numbered_text_cancel_and_guidance();
  test_line_shell_question_success_retry_multi_custom_and_cancel();
  test_line_shell_question_invalid_and_secret_fail_bounded();
}
