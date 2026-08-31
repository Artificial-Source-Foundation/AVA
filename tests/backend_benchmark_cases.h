#pragma once

#include "tests/backend_benchmark_protocol.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace ava::benchmark {

struct BackendBenchmarkOptions
{
  std::string benchmark_case;
  std::size_t iterations = 1;
  std::size_t entries = 0;
  std::size_t records = 0;
  std::size_t hold_milliseconds = 500;
  std::size_t grace_milliseconds = 75;
  std::size_t deadline_milliseconds = 2000;
  unsigned int loopback_port = 0;
  std::filesystem::path sample_plugin;
  std::filesystem::path fake_process_child;
  std::filesystem::path fake_mcp_server;
  std::filesystem::path fake_lsp_server;
};

[[nodiscard]] bool is_process_benchmark_case(std::string_view benchmark_case) noexcept;
[[nodiscard]] bool is_family_benchmark_case(std::string_view benchmark_case) noexcept;
void run_process_benchmark(BackendBenchmarkOptions const& options);
void run_family_benchmark(BackendBenchmarkOptions const& options);
void append_family_authorities(JsonFields& fields);

}  // namespace ava::benchmark
