#pragma once

#include "ava/core/result.h"

#include <atomic>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

namespace ava::app::rpc {

class RpcLineReader
{
 public:
  virtual ~RpcLineReader() = default;

  [[nodiscard]] virtual ava::core::Result<bool> read_line(std::string& line) = 0;
  virtual void cancel() noexcept = 0;
};

class StreamRpcLineReader final : public RpcLineReader
{
 public:
  explicit StreamRpcLineReader(std::istream& input, std::function<void()> wake = {});

  [[nodiscard]] ava::core::Result<bool> read_line(std::string& line) override;
  void cancel() noexcept override;

 private:
  std::istream& input_;
  std::function<void()> wake_;
  std::atomic_bool canceled_ = false;
};

[[nodiscard]] ava::core::Result<std::unique_ptr<RpcLineReader>> make_posix_rpc_line_reader(int input_fd);

}  // namespace ava::app::rpc
