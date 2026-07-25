#include "sys.h"
#include "ava/app/onboarding.h"
#include "ava/app/runtime/Session.h"
#include "ava/tui/terminal.h"
#include "ava/config/auth.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ava::app {
namespace {

std::string provider_env_key(std::string_view provider_id)
{
  if (provider_id == "openai")
    return "OPENAI_API_KEY";
  if (provider_id == "anthropic")
    return "ANTHROPIC_API_KEY";

  std::string key;
  key.reserve(provider_id.size() + std::string_view("_API_KEY").size());
  for (char const ch : provider_id)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) != 0)
    {
      key.push_back(static_cast<char>(std::toupper(byte)));
    }
    else if (ch == '-' || ch == '_')
    {
      key.push_back('_');
    }
  }
  key += "_API_KEY";
  return key;
}

std::string provider_connect_command(std::string_view provider_id)
{
  if (provider_id == "openai")
    return "ava connect openai --headless-oauth";
  return "printf '%s\\n' \"$" + provider_env_key(provider_id) + "\" | ava connect " + std::string(provider_id) + " --api-key-stdin";
}

std::string provider_display_name(std::string_view provider_id)
{
  if (provider_id == "openai")
    return "OpenAI";
  if (provider_id == "anthropic")
    return "Anthropic";
  auto display = std::string(provider_id);
  if (!display.empty())
    display.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(display.front())));
  return display;
}

std::string auth_setup_message(runtime::Session const& session, std::string_view prefix)
{
  auto const provider_id = session.model().provider_id.empty() ? std::string("openai") : session.model().provider_id;
  auto const env_key = provider_env_key(provider_id);
  std::string message(prefix);
  if (!message.empty() && message.back() != '\n')
    message.push_back('\n');
  message += "Connect with /connect or /login in this TUI, or run `" + provider_connect_command(provider_id) + "`.\n";
  message += "Environment setup also works with `" + env_key + "`.\n";
  message += "Auth file: " + session.paths().auth_file.string();
  return message;
}

}  // namespace

std::optional<std::string> first_run_auth_onboarding_message(runtime::Session const& session)
{
  auto credential = ava::config::provider_credential_for_startup(session.paths(), session.model().provider_id);
  auto const provider_id = session.model().provider_id.empty() ? std::string("openai") : session.model().provider_id;
  if (!credential)
    return "! " + provider_display_name(provider_id) + " auth unavailable · /connect";
  if (*credential)
    return std::nullopt;

  return "! " + provider_display_name(provider_id) + " not connected · /connect";
}

std::string provider_auth_required_message(runtime::Session const& session, std::string_view offline_suffix)
{
  auto const provider_id = session.model().provider_id.empty() ? std::string("openai") : session.model().provider_id;
  auto message = auth_setup_message(session, "Auth is required for provider `" + provider_id + "`.");
  if (!offline_suffix.empty())
  {
    message += std::string(offline_suffix);
  }
  return message;
}

}  // namespace ava::app
