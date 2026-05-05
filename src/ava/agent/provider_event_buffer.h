#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ava/agent/assistant_turn.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::agent {

using ProviderEventPublisher = std::function<ava::core::VoidResult(ava::provider::StreamEvent const& event)>;

class ProviderEventBuffer {
 public:
  explicit ProviderEventBuffer(ProviderOutputLimits limits);

  [[nodiscard]] std::vector<ava::provider::StreamEvent> const& events() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  [[nodiscard]] ava::core::VoidResult append(std::vector<ava::provider::StreamEvent> new_events,
                                             ProviderEventPublisher const& publisher, bool publish_all_events = true);

 private:
  ProviderOutputLimits limits_;
  std::vector<ava::provider::StreamEvent> events_;
  std::size_t assistant_text_bytes_ = 0;
  std::map<std::string, std::size_t> tool_argument_bytes_;
};

}  // namespace ava::agent
