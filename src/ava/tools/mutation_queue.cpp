#include "sys.h"
#include "ava/tools/mutation_queue.h"
#include "ava/core/path.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace ava::tools {

MutationQueue::Lock::Lock(std::shared_ptr<State> state, std::vector<std::shared_ptr<Entry>> entries, std::vector<std::unique_lock<std::mutex>> locks)
    : state_(std::move(state)), entries_(std::move(entries)), locks_(std::move(locks))
{
}

MutationQueue::Lock::~Lock()
{
  reset();
}

MutationQueue::Lock::Lock(Lock&& other) noexcept : state_(std::move(other.state_)), entries_(std::move(other.entries_)), locks_(std::move(other.locks_))
{
  other.state_.reset();
}

MutationQueue::Lock& MutationQueue::Lock::operator=(Lock&& other) noexcept
{
  if (this != &other)
  {
    reset();
    state_ = std::move(other.state_);
    entries_ = std::move(other.entries_);
    locks_ = std::move(other.locks_);
    other.state_.reset();
  }
  return *this;
}

void MutationQueue::Lock::reset() noexcept
{
  locks_.clear();
  entries_.clear();
  auto state = std::move(state_);
  if (!state)
    return;
  std::scoped_lock guard(state->entries_mutex);
  MutationQueue::prune_expired_entries(*state);
}

MutationQueue::Lock MutationQueue::lock_path(std::filesystem::path const& path)
{
  std::array const paths{path};
  return lock_paths(paths);
}

MutationQueue::Lock MutationQueue::lock_paths(std::span<std::filesystem::path const> paths)
{
  std::vector<std::filesystem::path> keys;
  keys.reserve(paths.size());
  for (auto const& path : paths)
  {
    keys.push_back(normalized_key(path));
  }
  std::ranges::sort(keys);
  keys.erase(std::ranges::unique(keys).begin(), keys.end());
  if (keys.empty())
    return Lock();

  std::vector<std::shared_ptr<Entry>> entries;
  entries.reserve(keys.size());
  auto state = state_;
  {
    std::scoped_lock guard(state->entries_mutex);
    prune_expired_entries(*state);
    for (auto const& key : keys)
    {
      auto found = std::ranges::find_if(state->entries, [&key](auto const& item) { return item.first == key; });
      if (found == state->entries.end())
      {
        auto entry = std::make_shared<Entry>();
        state->entries.push_back({key, entry});
        entries.push_back(std::move(entry));
        continue;
      }

      auto entry = found->second.lock();
      if (!entry)
      {
        entry = std::make_shared<Entry>();
        found->second = entry;
      }
      entries.push_back(std::move(entry));
    }
  }

  std::vector<std::unique_lock<std::mutex>> locks;
  locks.reserve(entries.size());
  for (auto& entry : entries)
  {
    locks.emplace_back(entry->mutex);
  }
  return Lock(std::move(state), std::move(entries), std::move(locks));
}

std::size_t MutationQueue::tracked_path_count() const
{
  std::scoped_lock guard(state_->entries_mutex);
  return state_->entries.size();
}

std::filesystem::path MutationQueue::normalized_key(std::filesystem::path const& path)
{
  return ava::core::normalized_absolute_path(path);
}

void MutationQueue::prune_expired_entries(State& state)
{
  state.entries.erase(std::ranges::remove_if(state.entries, [](auto const& item) { return item.second.expired(); }).begin(), state.entries.end());
}

std::shared_ptr<MutationQueue> default_mutation_queue()
{
  static auto queue = std::make_shared<MutationQueue>();
  return queue;
}

}  // namespace ava::tools
