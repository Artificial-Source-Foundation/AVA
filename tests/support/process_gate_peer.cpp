#include "process_gate.h"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

// Read the inherited process-gate descriptor used by this cross-language fixture.
int inherited_control_descriptor()
{
  char const* value = std::getenv("AVA_TEST_CONTROL_FD");
  if (value == nullptr || *value == '\0')
    throw std::runtime_error("AVA_TEST_CONTROL_FD is required");
  int descriptor = -1;
  char const* end = value + std::strlen(value);
  auto const [parsed_end, error] = std::from_chars(value, end, descriptor);
  if (error != std::errc{} || parsed_end != end || descriptor < 0)
    throw std::runtime_error("AVA_TEST_CONTROL_FD must contain a nonnegative inherited descriptor");
  return descriptor;
}

}  // namespace

// Exercise unordered, bidirectional, and idempotent process-gate operations for the Python harness regression test.
int main()
{
  try
  {
    ava::test::ProcessGateSet gates(inherited_control_descriptor());
    bool invalid_open_rejected = false;
    try
    {
      gates.open(64);
    }
    catch (std::runtime_error const&)
    {
      invalid_open_rejected = true;
    }
    if (!invalid_open_rejected)
      throw std::runtime_error("process gate 64 was not rejected");
    gates.open(7);
    gates.open(2);
    gates.open(12);
    gates.open(7);
    gates.wait(5, std::chrono::seconds(5));
    gates.open(3);
    gates.wait(1, std::chrono::seconds(5));
    gates.wait(1, std::chrono::milliseconds::zero());
    gates.open(63);
    return 0;
  }
  catch (std::exception const& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
