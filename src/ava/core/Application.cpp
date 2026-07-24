#include "sys.h"
#include "ava/core/Application.h"

#include <cstdlib>
#include "debug.h"

namespace ava::core {
namespace {

// These process-lifecycle values are non-atomic by design. main publishes the
// initialized Application before app::run starts workers, and revokes it only
// after app::run returns with all app::run-owned workers joined.
Application const* registered_application = nullptr;

#define AVA_REQUIRE_APPLICATION_LIFECYCLE(condition) \
  do                                                 \
  {                                                  \
    if (!(condition))                                \
    {                                                \
      ASSERT(condition);                             \
      std::abort();                                  \
    }                                                \
  } while (false)

}  // namespace

Application::Application()
{
  AVA_REQUIRE_APPLICATION_LIFECYCLE(registered_application == nullptr);
  registered_application = this;
}

Application::~Application() noexcept
{
  AVA_REQUIRE_APPLICATION_LIFECYCLE(registered_application == this);
  initialized_ = false;
  registered_application = nullptr;
}

void Application::initialize(int argc, char** argv)
{
  AVA_REQUIRE_APPLICATION_LIFECYCLE(registered_application == this);
  AVA_REQUIRE_APPLICATION_LIFECYCLE(!initialized_);
  AVA_REQUIRE_APPLICATION_LIFECYCLE(argc >= 0);
  AVA_REQUIRE_APPLICATION_LIFECYCLE(argv != nullptr);
  AVA_REQUIRE_APPLICATION_LIFECYCLE(argv[argc] == nullptr);
  for (int argument_index = 0; argument_index < argc; ++argument_index) AVA_REQUIRE_APPLICATION_LIFECYCLE(argv[argument_index] != nullptr);

  // Publication is the final initialization operation.
  initialized_ = true;
}

Application const& Application::instance()
{
  AVA_REQUIRE_APPLICATION_LIFECYCLE(registered_application != nullptr);
  AVA_REQUIRE_APPLICATION_LIFECYCLE(registered_application->initialized_);
  return *registered_application;
}

#undef AVA_REQUIRE_APPLICATION_LIFECYCLE

}  // namespace ava::core
