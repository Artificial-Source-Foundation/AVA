#pragma once

#include "utils/has_print_on.h"

namespace ava {

// Print objects from namespace ava, using ADL, if they have a print_on member.
using utils::has_print_on::operator<<;

namespace agent {
using utils::has_print_on::operator<<;
namespace tool_dispatch {
using utils::has_print_on::operator<<;
} //  namespace tool_dispatch
} // namespace agent

namespace app {
using utils::has_print_on::operator<<;
namespace rpc {
using utils::has_print_on::operator<<;
} // namespace rpc
namespace runtime {
using utils::has_print_on::operator<<;
} // namespace runtime
} // namespace app

namespace config {
using utils::has_print_on::operator<<;
} // namespace config

namespace context {
using utils::has_print_on::operator<<;
} // namespace context

namespace core {
using utils::has_print_on::operator<<;
namespace json {
using utils::has_print_on::operator<<;
} // namespace json
} // namespace core

namespace lsp {
using utils::has_print_on::operator<<;
} // namespace lsp

namespace mcp {
using utils::has_print_on::operator<<;
} // namespace mcp

namespace permissions {
using utils::has_print_on::operator<<;
} // namespace permissions

namespace plugin {
using utils::has_print_on::operator<<;
} // namespace plugin

namespace provider {
using utils::has_print_on::operator<<;
namespace detail {
} // namespace detail
} // namespace provider

namespace session {
using utils::has_print_on::operator<<;
} // namespace session

namespace tools {
using utils::has_print_on::operator<<;
namespace detail {
} // namespace detail
} // namespace tools

namespace tui {
using utils::has_print_on::operator<<;
namespace detail {
} // namespace detail
} // namespace tui

// These only occur in tests/.
#if 0
namespace test {
} // namespace test

namespace tests {
} // namespace tests
#endif

} // namespace ava
