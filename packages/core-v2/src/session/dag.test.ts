import { beforeEach, describe, expect, it } from 'vitest'
import { findRoot, flattenTree, getAncestors, getDepth, getDescendants } from './dag.js'
import { createSessionManager, type SessionManager } from './manager.js'
import type { SessionMeta, SessionState } from './types.js'

describe('Session DAG', () => {
  let sm: SessionManager
  let lookup: (id: string) => SessionState | null
  let toMeta: (s: SessionState) => SessionMeta

  beforeEach(() => {
    sm = createSessionManager()
    lookup = (id: string) => sm.get(id)
    toMeta = (s: SessionState) => ({
      id: s.id,
      name: s.name,
      messageCount: s.messages.length,
      workingDirectory: s.workingDirectory,
      createdAt: s.createdAt,
      updatedAt: s.updatedAt,
      status: s.status,
      parentSessionId: s.parentSessionId,
      branchName: s.branchName,
      childCount: s.children?.length ?? 0,
    })
  })

  // ─── fork ─────────────────────────────────────────────────────────────

  describe('fork', () => {
    it('creates a child session from parent', () => {
      const parent = sm.create('parent', '/tmp')
      sm.addMessage(parent.id, { role: 'user', content: 'hello' })
      sm.addMessage(parent.id, { role: 'assistant', content: 'hi' })

      const child = sm.fork(parent.id, 'branch-1')
      expect(child.parentSessionId).toBe(parent.id)
      expect(child.branchName).toBe('branch-1')
      expect(child.branchPoint).toBe(2)
      expect(child.messages).toHaveLength(2)
    })

    it('forks at specific message index', () => {
      const parent = sm.create('parent', '/tmp')
      sm.addMessage(parent.id, { role: 'user', content: 'msg1' })
      sm.addMessage(parent.id, { role: 'assistant', content: 'msg2' })
      sm.addMessage(parent.id, { role: 'user', content: 'msg3' })

      const child = sm.fork(parent.id, 'branch-at-1', 1)
      expect(child.branchPoint).toBe(1)
      expect(child.messages).toHaveLength(1)
      expect(child.messages[0].content).toBe('msg1')
    })

    it('tracks child in parent', () => {
      const parent = sm.create('parent', '/tmp')
      const child = sm.fork(parent.id, 'branch-1')
      expect(parent.children).toContain(child.id)
    })

    it('inherits working directory', () => {
      const parent = sm.create('parent', '/home/user/project')
      const child = sm.fork(parent.id, 'branch-1')
      expect(child.workingDirectory).toBe('/home/user/project')
    })

    it('inherits env variables', () => {
      const parent = sm.create('parent', '/tmp')
      sm.setEnv(parent.id, 'FOO', 'bar')
      const child = sm.fork(parent.id, 'branch-1')
      expect(child.env.FOO).toBe('bar')
    })

    it('generates slug from branch name', () => {
      const parent = sm.create('parent', '/tmp')
      const child = sm.fork(parent.id, 'Fix login bug')
      expect(child.slug).toBe('fix-login-bug')
    })

    it('starts with active status', () => {
      const parent = sm.create('parent', '/tmp')
      const child = sm.fork(parent.id, 'branch-1')
      expect(child.status).toBe('active')
    })

    it('starts with zero tool call count', () => {
      const parent = sm.create('parent', '/tmp')
      const child = sm.fork(parent.id, 'branch-1')
      expect(child.toolCallCount).toBe(0)
    })
  })

  // ─── getBranches ──────────────────────────────────────────────────────

  describe('getBranches', () => {
    it('returns empty for session with no children', () => {
      const session = sm.create('solo', '/tmp')
      expect(sm.getBranches(session.id)).toEqual([])
    })

    it('returns direct children as metadata', () => {
      const parent = sm.create('parent', '/tmp')
      sm.fork(parent.id, 'branch-1')
      sm.fork(parent.id, 'branch-2')

      const branches = sm.getBranches(parent.id)
      expect(branches).toHaveLength(2)
      expect(branches[0].branchName).toBe('branch-1')
      expect(branches[1].branchName).toBe('branch-2')
    })

    it('returns empty for unknown session', () => {
      expect(sm.getBranches('nonexistent')).toEqual([])
    })
  })

  // ─── getTree ──────────────────────────────────────────────────────────

  describe('getTree', () => {
    it('returns single-node tree for leaf session', () => {
      const session = sm.create('leaf', '/tmp')
      const tree = sm.getTree(session.id)
      expect(tree).toHaveLength(1)
      expect(tree[0].id).toBe(session.id)
    })

    it('returns full tree for 3-depth DAG', () => {
      const root = sm.create('root', '/tmp')
      const child1 = sm.fork(root.id, 'child-1')
      const child2 = sm.fork(root.id, 'child-2')
      const grandchild = sm.fork(child1.id, 'grandchild')

      const tree = sm.getTree(root.id)
      expect(tree).toHaveLength(4)
      const ids = tree.map((t) => t.id)
      expect(ids).toContain(root.id)
      expect(ids).toContain(child1.id)
      expect(ids).toContain(child2.id)
      expect(ids).toContain(grandchild.id)
    })

    it('returns empty for unknown session', () => {
      expect(sm.getTree('nonexistent')).toEqual([])
    })
  })

  // ─── getAncestors ────────────────────────────────────────────────────

  describe('getAncestors', () => {
    it('returns just self for root session', () => {
      const root = sm.create('root', '/tmp')
      const ancestors = getAncestors(root.id, lookup)
      expect(ancestors).toEqual([root.id])
    })

    it('returns chain from leaf to root', () => {
      const root = sm.create('root', '/tmp')
      const child = sm.fork(root.id, 'child')
      const grandchild = sm.fork(child.id, 'grandchild')

      const ancestors = getAncestors(grandchild.id, lookup)
      expect(ancestors).toEqual([grandchild.id, child.id, root.id])
    })

    it('returns empty for unknown session', () => {
      expect(getAncestors('nonexistent', lookup)).toEqual(['nonexistent'])
    })
  })

  // ─── getDescendants ──────────────────────────────────────────────────

  describe('getDescendants', () => {
    it('returns empty for leaf session', () => {
      const leaf = sm.create('leaf', '/tmp')
      expect(getDescendants(leaf.id, lookup)).toEqual([])
    })

    it('returns all descendants breadth-first', () => {
      const root = sm.create('root', '/tmp')
      const child1 = sm.fork(root.id, 'child-1')
      const child2 = sm.fork(root.id, 'child-2')
      const grandchild = sm.fork(child1.id, 'grandchild')

      const descendants = getDescendants(root.id, lookup)
      expect(descendants).toHaveLength(3)
      expect(descendants).toContain(child1.id)
      expect(descendants).toContain(child2.id)
      expect(descendants).toContain(grandchild.id)
    })
  })

  // ─── flattenTree ─────────────────────────────────────────────────────

  describe('flattenTree', () => {
    it('returns single meta for leaf', () => {
      const leaf = sm.create('leaf', '/tmp')
      const flat = flattenTree(leaf.id, lookup, toMeta)
      expect(flat).toHaveLength(1)
    })

    it('flattens 3-depth tree depth-first', () => {
      const root = sm.create('root', '/tmp')
      const child1 = sm.fork(root.id, 'child-1')
      sm.fork(child1.id, 'grandchild')
      sm.fork(root.id, 'child-2')

      const flat = flattenTree(root.id, lookup, toMeta)
      expect(flat).toHaveLength(4)
      // Depth-first: root, child-1, grandchild, child-2
      expect(flat[0].id).toBe(root.id)
      expect(flat[1].id).toBe(child1.id)
    })
  })

  // ─── findRoot ─────────────────────────────────────────────────────────

  describe('findRoot', () => {
    it('returns self for root session', () => {
      const root = sm.create('root', '/tmp')
      expect(findRoot(root.id, lookup)).toBe(root.id)
    })

    it('finds root from grandchild', () => {
      const root = sm.create('root', '/tmp')
      const child = sm.fork(root.id, 'child')
      const grandchild = sm.fork(child.id, 'grandchild')
      expect(findRoot(grandchild.id, lookup)).toBe(root.id)
    })
  })

  // ─── getDepth ─────────────────────────────────────────────────────────

  describe('getDepth', () => {
    it('root has depth 0', () => {
      const root = sm.create('root', '/tmp')
      expect(getDepth(root.id, lookup)).toBe(0)
    })

    it('child has depth 1', () => {
      const root = sm.create('root', '/tmp')
      const child = sm.fork(root.id, 'child')
      expect(getDepth(child.id, lookup)).toBe(1)
    })

    it('grandchild has depth 2', () => {
      const root = sm.create('root', '/tmp')
      const child = sm.fork(root.id, 'child')
      const grandchild = sm.fork(child.id, 'grandchild')
      expect(getDepth(grandchild.id, lookup)).toBe(2)
    })
  })
})
