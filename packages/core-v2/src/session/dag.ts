/**
 * Session DAG traversal helpers — ancestors, descendants, tree flattening.
 *
 * Sessions form a DAG via parentSessionId/children links.
 * These helpers operate on a lookup function rather than the manager directly,
 * making them testable in isolation.
 */

import type { SessionMeta, SessionState } from './types.js'

export type SessionLookup = (id: string) => SessionState | null

/** Get all ancestor session IDs from child to root (inclusive). */
export function getAncestors(sessionId: string, lookup: SessionLookup): string[] {
  const ancestors: string[] = []
  let currentId: string | undefined = sessionId
  const visited = new Set<string>()

  while (currentId) {
    if (visited.has(currentId)) break // cycle protection
    visited.add(currentId)
    ancestors.push(currentId)
    const session = lookup(currentId)
    currentId = session?.parentSessionId
  }

  return ancestors
}

/** Get all descendant session IDs (breadth-first, excludes root). */
export function getDescendants(rootId: string, lookup: SessionLookup): string[] {
  const descendants: string[] = []
  const queue = [rootId]
  const visited = new Set<string>()
  visited.add(rootId)

  while (queue.length > 0) {
    const currentId = queue.shift()!
    const session = lookup(currentId)
    if (!session?.children) continue

    for (const childId of session.children) {
      if (visited.has(childId)) continue
      visited.add(childId)
      descendants.push(childId)
      queue.push(childId)
    }
  }

  return descendants
}

/** Flatten a session tree into a flat array of metas (depth-first pre-order). */
export function flattenTree(
  rootId: string,
  lookup: SessionLookup,
  toMeta: (s: SessionState) => SessionMeta
): SessionMeta[] {
  const result: SessionMeta[] = []
  const visited = new Set<string>()

  function visit(id: string): void {
    if (visited.has(id)) return
    visited.add(id)
    const session = lookup(id)
    if (!session) return
    result.push(toMeta(session))
    if (session.children) {
      for (const childId of session.children) {
        visit(childId)
      }
    }
  }

  visit(rootId)
  return result
}

/** Find the root of a session's tree (the ancestor with no parent). */
export function findRoot(sessionId: string, lookup: SessionLookup): string {
  let currentId = sessionId
  const visited = new Set<string>()

  while (true) {
    if (visited.has(currentId)) return currentId // cycle — return current
    visited.add(currentId)
    const session = lookup(currentId)
    if (!session?.parentSessionId) return currentId
    currentId = session.parentSessionId
  }
}

/** Get the depth of a session in its tree (root = 0). */
export function getDepth(sessionId: string, lookup: SessionLookup): number {
  const ancestors = getAncestors(sessionId, lookup)
  return ancestors.length - 1 // first element is self
}
