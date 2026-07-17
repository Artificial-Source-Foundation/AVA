#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <atomic>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

namespace ava::app::rpc {

enum class RpcInputTerminalOutcome
{
  Eof,
  EofWithFinalRecord,
  Canceled,
  Error,
};

using RpcInputTerminalCallback = std::function<void(RpcInputTerminalOutcome)>;

class RpcLineReader
{
 public:
  virtual ~RpcLineReader() = default;

  // `on_terminal` is invoked synchronously for terminal input boundaries only and is never retained.
  // It must not throw.
  [[nodiscard]] virtual ava::core::Result<bool> read_line(std::string& line, RpcInputTerminalCallback const& on_terminal = {}) = 0;
  virtual void cancel() noexcept = 0;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

class StreamRpcLineReader final : public RpcLineReader
{
 public:
  explicit StreamRpcLineReader(std::istream& input, std::function<void()> wake = {});

  [[nodiscard]] ava::core::Result<bool> read_line(std::string& line, RpcInputTerminalCallback const& on_terminal = {}) override;
  void cancel() noexcept override;

 private:
  std::istream& input_;
  std::function<void()> wake_;
  std::atomic_bool canceled_ = false;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] ava::core::Result<std::unique_ptr<RpcLineReader>> make_posix_rpc_line_reader(int input_fd);

}  // namespace ava::app::rpc
