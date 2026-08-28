#pragma once

#include "ava/core/Application.h"
#include "memory/MemoryPagePool.h"
#include "memory/NodeMemoryResource.h"

#include <string_view>
#include "debug.h"

namespace ava::app {

class Application final : public ava::core::Application
{
 public:
  static constexpr std::size_t mpp_page_size = 0x8000;          // Allocate 32kB at a time (or larger powers of two).

 private:
  memory::MemoryPagePool mpp_;
  memory::NodeMemoryResource nmr_;

 public:
  Application() : mpp_(mpp_page_size), nmr_(mpp_) { }

  [[nodiscard]] std::string_view application_name() const noexcept override { return "AVA"; }

  // Can't print mpp_.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::app
