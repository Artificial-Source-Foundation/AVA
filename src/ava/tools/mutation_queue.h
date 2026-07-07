#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace ava::tools {

class MutationQueue
{
 private:
  struct Entry;
  struct State;

 public:
  class Lock
  {
   public:
    Lock() = default;
    ~Lock();

    Lock(Lock const&) = delete;
    Lock& operator=(Lock const&) = delete;
    Lock(Lock&& other) noexcept;
    Lock& operator=(Lock&& other) noexcept;

   private:
    friend class MutationQueue;

    Lock(std::shared_ptr<State> state, std::vector<std::shared_ptr<Entry>> entries, std::vector<std::unique_lock<std::mutex>> locks);

    void reset() noexcept;

    std::shared_ptr<State> state_;
    std::vector<std::shared_ptr<Entry>> entries_;
    std::vector<std::unique_lock<std::mutex>> locks_;
  };

  MutationQueue() = default;
  MutationQueue(MutationQueue const&) = delete;
  MutationQueue& operator=(MutationQueue const&) = delete;
  MutationQueue(MutationQueue&&) = delete;
  MutationQueue& operator=(MutationQueue&&) = delete;

  [[nodiscard]] Lock lock_path(std::filesystem::path const& path);
  [[nodiscard]] Lock lock_paths(std::span<std::filesystem::path const> paths);
  [[nodiscard]] std::size_t tracked_path_count() const;

 private:
  struct Entry
  {
    std::mutex mutex;
  };

  struct State
  {
    std::mutex entries_mutex;
    std::vector<std::pair<std::filesystem::path, std::weak_ptr<Entry>>> entries;
  };

  [[nodiscard]] static std::filesystem::path normalized_key(std::filesystem::path const& path);
  static void prune_expired_entries(State& state);

  std::shared_ptr<State> state_ = std::make_shared<State>();
};

[[nodiscard]] std::shared_ptr<MutationQueue> default_mutation_queue();

}  // namespace ava::tools
