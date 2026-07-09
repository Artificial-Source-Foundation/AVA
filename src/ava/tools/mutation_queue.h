#pragma once

#include "ava/debug/print_members_on.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace ava::tools {

class MutationQueue
{
 public:
  class Lock
  {
   public:
    Lock() = default;
    explicit Lock(std::vector<std::unique_lock<std::mutex>> locks);

    Lock(Lock const&) = delete;
    Lock& operator=(Lock const&) = delete;
    Lock(Lock&&) noexcept = default;
    Lock& operator=(Lock&&) noexcept = default;

   private:
    std::vector<std::unique_lock<std::mutex>> locks_;
  };

  [[nodiscard]] Lock lock_path(std::filesystem::path const& path);
  [[nodiscard]] Lock lock_paths(std::span<std::filesystem::path const> paths);

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  struct Entry
  {
    std::mutex mutex;
  };

  [[nodiscard]] static std::filesystem::path normalized_key(std::filesystem::path const& path);

  std::mutex entries_mutex_;
  std::vector<std::pair<std::filesystem::path, std::shared_ptr<Entry>>> entries_;
};

[[nodiscard]] std::shared_ptr<MutationQueue> default_mutation_queue();

}  // namespace ava::tools
