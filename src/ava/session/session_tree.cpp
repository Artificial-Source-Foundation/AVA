#include "ava/session/session_tree.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ava::session {
namespace {

std::vector<std::string> current_path_for(std::vector<SessionTreeNode> const& nodes, std::unordered_map<std::string, std::size_t> const& index_by_id,
                                          std::unordered_set<std::string> const& cyclic_chains, std::string_view current_session_id)
{
  if (cyclic_chains.contains(std::string(current_session_id)))
  {
    return index_by_id.contains(std::string(current_session_id)) ? std::vector<std::string>{std::string(current_session_id)} : std::vector<std::string>{};
  }

  std::vector<std::string> reversed;
  std::unordered_set<std::string> seen;
  std::string cursor(current_session_id);
  while (!cursor.empty())
  {
    if (!seen.insert(cursor).second)
      break;
    auto const found = index_by_id.find(cursor);
    if (found == index_by_id.end())
      break;
    reversed.push_back(cursor);
    cursor = nodes[found->second].metadata.parent_session_id;
  }
  std::ranges::reverse(reversed);
  return reversed;
}

bool parent_chain_has_cycle(std::vector<SessionTreeNode> const& nodes, std::unordered_map<std::string, std::size_t> const& index_by_id,
                            std::string_view session_id)
{
  std::unordered_set<std::string> seen;
  std::string cursor(session_id);
  while (!cursor.empty())
  {
    if (!seen.insert(cursor).second)
      return true;
    auto const found = index_by_id.find(cursor);
    if (found == index_by_id.end())
      return false;
    cursor = nodes[found->second].metadata.parent_session_id;
  }
  return false;
}

}  // namespace

ava::core::Result<SessionTreeIndex> build_session_tree(std::filesystem::path const& workspace_dir, std::filesystem::path const& root_dir,
                                                       std::string_view current_session_id)
{
  auto summaries = SessionStore::list_sessions(workspace_dir, root_dir);
  if (!summaries)
    return std::unexpected(std::move(summaries.error()));

  SessionTreeIndex index;
  index.current_session_id = std::string(current_session_id);
  index.sessions.reserve(summaries->size());
  std::unordered_map<std::string, std::size_t> index_by_id;
  for (auto const& summary : *summaries)
  {
    auto store = SessionStore::open(workspace_dir, summary.session_id, root_dir);
    if (!store)
      continue;
    auto metadata = load_session_metadata(*store);
    if (!metadata)
      continue;
    auto const node_index = index.sessions.size();
    index_by_id.emplace(summary.session_id, node_index);
    index.sessions.push_back(
        SessionTreeNode{.summary = summary, .metadata = std::move(*metadata), .children = {}, .current = summary.session_id == current_session_id});
  }

  std::unordered_set<std::string> cyclic_chains;
  for (auto const& node : index.sessions)
  {
    if (parent_chain_has_cycle(index.sessions, index_by_id, node.summary.session_id))
    {
      cyclic_chains.insert(node.summary.session_id);
    }
  }

  for (auto& node : index.sessions)
  {
    auto const& parent = node.metadata.parent_session_id;
    auto const parent_found = index_by_id.find(parent);
    if (parent.empty() || parent_found == index_by_id.end() || cyclic_chains.contains(node.summary.session_id))
    {
      index.roots.push_back(node.summary.session_id);
      continue;
    }
    index.sessions[parent_found->second].children.push_back(node.summary.session_id);
  }

  for (auto const& node : index.sessions)
  {
    if (node.children.empty())
      index.leaves.push_back(node.summary.session_id);
  }

  if (!current_session_id.empty())
  {
    index.current_path = current_path_for(index.sessions, index_by_id, cyclic_chains, current_session_id);
  }
  return index;
}

}  // namespace ava::session
