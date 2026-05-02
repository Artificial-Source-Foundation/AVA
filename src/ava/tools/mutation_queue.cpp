#include "ava/tools/mutation_queue.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace ava::tools {

MutationQueue::Lock::Lock(std::vector<std::unique_lock<std::mutex>> locks) : locks_(std::move(locks)) {}

MutationQueue::Lock MutationQueue::lock_path(const std::filesystem::path& path) {
  const std::array paths{path};
  return lock_paths(paths);
}

MutationQueue::Lock MutationQueue::lock_paths(std::span<const std::filesystem::path> paths) {
  std::vector<std::filesystem::path> keys;
  keys.reserve(paths.size());
  for (const auto& path : paths) {
    keys.push_back(normalized_key(path));
  }
  std::ranges::sort(keys);
  keys.erase(std::ranges::unique(keys).begin(), keys.end());

  std::vector<std::shared_ptr<Entry>> entries;
  entries.reserve(keys.size());
  {
    std::scoped_lock guard(entries_mutex_);
    for (const auto& key : keys) {
      auto found = std::ranges::find_if(entries_, [&key](const auto& item) { return item.first == key; });
      if (found == entries_.end()) {
        found = entries_.insert(entries_.end(), {key, std::make_shared<Entry>()});
      }
      entries.push_back(found->second);
    }
  }

  std::vector<std::unique_lock<std::mutex>> locks;
  locks.reserve(entries.size());
  for (auto& entry : entries) {
    locks.emplace_back(entry->mutex);
  }
  return Lock(std::move(locks));
}

std::filesystem::path MutationQueue::normalized_key(const std::filesystem::path& path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical.lexically_normal();
  auto absolute = std::filesystem::absolute(path, error);
  if (!error) return absolute.lexically_normal();
  return path.lexically_normal();
}

std::shared_ptr<MutationQueue> default_mutation_queue() {
  static auto queue = std::make_shared<MutationQueue>();
  return queue;
}

}  // namespace ava::tools
