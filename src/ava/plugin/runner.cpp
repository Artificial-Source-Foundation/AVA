#include "ava/plugin/runner.h"

#include <utility>

#include "ava/plugin/runner_support.h"

namespace ava::plugin {

PluginProcess::PluginProcess(PluginManifest manifest, PluginRunnerOptions options)
    : manifest_(std::move(manifest)),
      options_(std::move(options)),
      stdout_buffer_(options_.max_record_bytes),
      stderr_tail_(options_.max_stderr_bytes)
{
}

PluginProcess::~PluginProcess()
{
  terminate_child();
  close_fds();
}

ava::core::Result<std::unique_ptr<PluginProcess>> PluginProcess::start(PluginManifest manifest,
                                                                       PluginRunnerOptions options,
                                                                       CancelCallback cancel_requested)
{
  if (auto valid = detail::validate_start_request(manifest, options, cancel_requested); !valid) {
    return std::unexpected(std::move(valid.error()));
  }

  auto process = std::unique_ptr<PluginProcess>(new PluginProcess(std::move(manifest), std::move(options)));
  if (auto launched = process->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (auto initialized = process->initialize(cancel_requested); !initialized) {
    return std::unexpected(std::move(initialized.error()));
  }
  return process;
}

PluginManifest const& PluginProcess::manifest() const noexcept
{
  return manifest_;
}

PluginInitialization const& PluginProcess::initialization() const noexcept
{
  return initialization_;
}

std::string const& PluginProcess::stderr_tail() const noexcept
{
  return stderr_tail_.text();
}

bool PluginProcess::stderr_truncated() const noexcept
{
  return stderr_tail_.truncated();
}

}  // namespace ava::plugin
