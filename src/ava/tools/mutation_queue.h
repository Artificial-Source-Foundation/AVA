#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace ava::tools {

class MutationQueue {
 public:
  class Lock {
   public:
    Lock() = default;
    explicit Lock(std::vector<std::unique_lock<std::mutex>> locks);

    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) noexcept = default;
    Lock& operator=(Lock&&) noexcept = default;

   private:
    std::vector<std::unique_lock<std::mutex>> locks_;
  };

  [[nodiscard]] Lock lock_path(const std::filesystem::path& path);
  [[nodiscard]] Lock lock_paths(std::span<const std::filesystem::path> paths);

 private:
  struct Entry {
    std::mutex mutex;
  };

  [[nodiscard]] static std::filesystem::path normalized_key(const std::filesystem::path& path);

  std::mutex entries_mutex_;
  std::vector<std::pair<std::filesystem::path, std::shared_ptr<Entry>>> entries_;
};

[[nodiscard]] std::shared_ptr<MutationQueue> default_mutation_queue();

}  // namespace ava::tools
