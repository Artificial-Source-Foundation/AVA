#pragma once

#include "ava/event/RuntimeEvent.h"

#include <type_traits>
#include <variant>

namespace ava::tests {

template <ava::event::RuntimeEventAlternative Event>
[[nodiscard]] std::remove_cvref_t<Event> const* runtime_event_as(ava::event::RuntimeEvent const& event) noexcept
{
  return std::get_if<std::remove_cvref_t<Event>>(&event.payload());
}

}  // namespace ava::tests
