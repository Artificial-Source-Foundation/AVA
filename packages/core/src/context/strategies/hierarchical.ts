/**
 * Hierarchical Compaction Strategy (Goose-inspired)
 *
 * Builds a tree of progressively summarized content.
 * Lower levels have detailed summaries, higher levels are more compressed.
 * Selects appropriate level based on available token budget.
 *
 * Pros:
 * - Maintains multiple levels of detail
 * - Can quickly adapt to different token budgets
 * - Preserves structure across the conversation
 *
 * Cons:
 * - More complex implementation
 * - Requires building and maintaining the tree
 * - Higher initial LLM cost
 */

import { countTokens } from '../tracker.js'
import type {
  CompactionStrategy,
  Message,
  SummarizeFn,
  SummaryNode,
  SummaryTree,
} from '../types.js'

// ============================================================================
// Configuration
// ============================================================================

export interface HierarchicalConfig {
  /** Number of messages per leaf node (default: 4 = 2 turns) */
  messagesPerLeaf?: number
  /** Maximum tree depth (default: 4) */
  maxDepth?: number
  /** Number of recent messages to always preserve (default: 4) */
  preserveRecent?: number
  /** Function to summarize messages */
  summarizeFn?: SummarizeFn
}

// ============================================================================
// Tree Building
// ============================================================================

/**
 * Build a hierarchical summary tree from messages
 */
export async function buildSummaryTree(
  messages: Message[],
  config: Required<Pick<HierarchicalConfig, 'messagesPerLeaf' | 'maxDepth' | 'summarizeFn'>>
): Promise<SummaryTree> {
  const { messagesPerLeaf, maxDepth, summarizeFn } = config

  const nodes = new Map<string, SummaryNode>()

  // Create leaf nodes from message groups
  const leafNodes: SummaryNode[] = []

  for (let i = 0; i < messages.length; i += messagesPerLeaf) {
    const group = messages.slice(i, i + messagesPerLeaf)
    if (group.length === 0) continue

    const nodeId = `leaf-${i}`
    const summary = await summarizeFn(group)
    const tokenCount = countTokens(summary)

    const node: SummaryNode = {
      id: nodeId,
      level: 0,
      summary,
      tokenCount,
      messageIds: group.map((m) => m.id),
    }

    leafNodes.push(node)
    nodes.set(nodeId, node)
  }

  // Build higher levels by combining nodes
  let currentLevel = leafNodes
  let level = 1

  while (currentLevel.length > 1 && level <= maxDepth) {
    const nextLevel: SummaryNode[] = []

    // Group pairs of nodes
    for (let i = 0; i < currentLevel.length; i += 2) {
      const children = currentLevel.slice(i, i + 2)
      if (children.length === 0) continue

      // Create fake messages for summarization (combining child summaries)
      const fakeMessages: Message[] = children.map((c) => ({
        id: c.id,
        sessionId: 'tree',
        role: 'assistant' as const,
        content: c.summary,
        createdAt: Date.now(),
      }))

      const summary = await summarizeFn(fakeMessages)
      const tokenCount = countTokens(summary)
      const nodeId = `level${level}-${i}`

      const node: SummaryNode = {
        id: nodeId,
        level,
        summary,
        tokenCount,
        children: children.map((c) => c.id),
      }

      nextLevel.push(node)
      nodes.set(nodeId, node)
    }

    if (nextLevel.length === 0) break
    currentLevel = nextLevel
    level++
  }

  // Root is the last single node (or the last level if multiple)
  const rootId = currentLevel.length === 1 ? currentLevel[0].id : `root-${Date.now()}`

  // If multiple nodes at top, create a root combining them
  if (currentLevel.length > 1) {
    const fakeMessages: Message[] = currentLevel.map((c) => ({
      id: c.id,
      sessionId: 'tree',
      role: 'assistant' as const,
      content: c.summary,
      createdAt: Date.now(),
    }))

    const summary = await summarizeFn(fakeMessages)
    const rootNode: SummaryNode = {
      id: rootId,
      level,
      summary,
      tokenCount: countTokens(summary),
      children: currentLevel.map((c) => c.id),
    }
    nodes.set(rootId, rootNode)
  }

  return {
    rootId,
    nodes,
    maxLevel: level,
  }
}

/**
 * Select the best summary level for target tokens
 */
export function selectLevel(tree: SummaryTree, targetTokens: number): SummaryNode[] {
  // Start from root and descend until we exceed budget
  const root = tree.nodes.get(tree.rootId)
  if (!root) return []

  // Try each level from highest (most compressed) to lowest (most detailed)
  for (let level = tree.maxLevel; level >= 0; level--) {
    const nodesAtLevel = Array.from(tree.nodes.values()).filter((n) => n.level === level)

    const totalTokens = nodesAtLevel.reduce((sum, n) => sum + n.tokenCount, 0)

    if (totalTokens <= targetTokens) {
      return nodesAtLevel
    }
  }

  // Fallback: just return root
  return [root]
}

// ============================================================================
// Strategy Implementation
// ============================================================================

/**
 * Create a hierarchical compaction strategy
 *
 * @example
 * ```ts
 * const strategy = createHierarchical({
 *   messagesPerLeaf: 4,
 *   maxDepth: 3,
 *   summarizeFn: async (messages) => await myLLM.summarize(messages)
 * })
 * ```
 */
export function createHierarchical(config: HierarchicalConfig = {}): CompactionStrategy {
  const {
    messagesPerLeaf = 4,
    maxDepth = 4,
    preserveRecent = 4,
    summarizeFn = async () => {
      throw new Error('No summarizeFn provided to hierarchical strategy')
    },
  } = config

  // Cache for built trees (by session)
  const treeCache = new Map<string, SummaryTree>()

  return {
    name: 'hierarchical',

    async compact(messages: Message[], targetTokens: number): Promise<Message[]> {
      if (messages.length === 0) {
        return []
      }

      // Separate system message
      const systemMessage = messages.find((m) => m.role === 'system')
      const conversationMessages = messages.filter((m) => m.role !== 'system')

      // Preserve recent messages
      const recentMessages = conversationMessages.slice(-preserveRecent)
      const olderMessages = conversationMessages.slice(0, -preserveRecent)

      if (olderMessages.length === 0) {
        return systemMessage ? [systemMessage, ...recentMessages] : recentMessages
      }

      // Calculate budget for summaries
      const systemTokens = systemMessage ? countTokens(systemMessage.content) : 0
      const recentTokens = recentMessages.reduce(
        (sum, m) => sum + (m.tokenCount ?? countTokens(m.content)),
        0
      )
      const summaryBudget = targetTokens - systemTokens - recentTokens

      if (summaryBudget <= 0) {
        // No room for summaries, just keep recent
        return systemMessage ? [systemMessage, ...recentMessages] : recentMessages
      }

      // Build or get cached tree
      const sessionId = messages[0].sessionId
      let tree = treeCache.get(sessionId)

      if (!tree || olderMessages.length !== tree.nodes.size) {
        tree = await buildSummaryTree(olderMessages, {
          messagesPerLeaf,
          maxDepth,
          summarizeFn,
        })
        treeCache.set(sessionId, tree)
      }

      // Select appropriate level
      const selectedNodes = selectLevel(tree, summaryBudget)

      // Combine selected summaries
      const combinedSummary = selectedNodes.map((n) => n.summary).join('\n\n')

      const summaryMessage: Message = {
        id: `hierarchical-summary-${Date.now()}`,
        sessionId,
        role: 'system',
        content: `[Conversation history - hierarchical summary]\n${combinedSummary}`,
        createdAt: Date.now(),
        tokenCount: countTokens(combinedSummary),
      }

      // Combine: system + summary + recent
      const result: Message[] = []

      if (systemMessage) {
        result.push(systemMessage)
      }

      result.push(summaryMessage)
      result.push(...recentMessages)

      return result
    },
  }
}

/**
 * Clear hierarchical strategy cache for a session
 */
export function clearHierarchicalCache(sessionId: string): void {
  // Note: This is a module-level function that doesn't access the strategy's cache
  // In practice, you'd want to expose this from the strategy instance
  console.warn(
    `clearHierarchicalCache: Cannot clear cache for session ${sessionId} from outside strategy`
  )
}

// ============================================================================
// Default Export (requires summarizeFn)
// ============================================================================

/**
 * Default hierarchical strategy instance
 * Note: Requires providing summarizeFn in options
 */
export const hierarchical = createHierarchical()

export default hierarchical
