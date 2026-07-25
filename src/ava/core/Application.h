#pragma once

#include <string_view>
#include "debug.h"

namespace ava::core {

// Process-lifetime interface published by the production composition root.
class Application
{
 public:
  Application();
  Application(Application const&) = delete;
  Application& operator=(Application const&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;
  virtual ~Application() noexcept;

  void initialize(int argc, char** argv);

  [[nodiscard]] static Application const& instance();
  [[nodiscard]] virtual std::string_view application_name() const noexcept = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  bool initialized_ = false;
};

}  // namespace ava::core
