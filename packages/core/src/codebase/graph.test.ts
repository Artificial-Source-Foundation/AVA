/**
 * Dependency Graph Tests
 */

import { describe, expect, it } from 'vitest'
import {
  findCircularDependencies,
  findLeaves,
  findRoots,
  getDependencyDepth,
  getEdges,
  getGraphStats,
  getTransitiveDependencies,
  getTransitiveDependents,
} from './graph.js'
import type { DependencyNode } from './types.js'

// ============================================================================
// Helper
// ============================================================================

function makeGraph(edges: Record<string, string[]>): Map<string, DependencyNode> {
  const graph = new Map<string, DependencyNode>()
  for (const file of Object.keys(edges)) {
    graph.set(file, { file, imports: [], importedBy: [], rank: 0 })
  }
  for (const [file, imports] of Object.entries(edges)) {
    const node = graph.get(file)!
    node.imports = imports
    for (const imp of imports) {
      const target = graph.get(imp)
      if (target) {
        target.importedBy.push(file)
      }
    }
  }
  return graph
}

// ============================================================================
// getEdges
// ============================================================================

describe('getEdges', () => {
  it('returns all edges', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': ['c.ts'], 'c.ts': [] })
    const edges = getEdges(graph)
    expect(edges).toHaveLength(2)
    expect(edges.find((e) => e.from === 'a.ts' && e.to === 'b.ts')).toBeDefined()
  })

  it('returns empty for graph with no edges', () => {
    const graph = makeGraph({ 'a.ts': [], 'b.ts': [] })
    expect(getEdges(graph)).toHaveLength(0)
  })
})

// ============================================================================
// findRoots
// ============================================================================

describe('findRoots', () => {
  it('finds files with no dependents', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    const roots = findRoots(graph)
    expect(roots).toContain('a.ts')
    expect(roots).not.toContain('b.ts')
  })

  it('returns all files if none are imported', () => {
    const graph = makeGraph({ 'a.ts': [], 'b.ts': [], 'c.ts': [] })
    expect(findRoots(graph)).toHaveLength(3)
  })
})

// ============================================================================
// findLeaves
// ============================================================================

describe('findLeaves', () => {
  it('finds files with no imports', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    const leaves = findLeaves(graph)
    expect(leaves).toContain('b.ts')
    expect(leaves).not.toContain('a.ts')
  })

  it('returns all files if none import anything', () => {
    const graph = makeGraph({ 'a.ts': [], 'b.ts': [] })
    expect(findLeaves(graph)).toHaveLength(2)
  })
})

// ============================================================================
// findCircularDependencies
// ============================================================================

describe('findCircularDependencies', () => {
  it('returns empty for acyclic graph', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': ['c.ts'], 'c.ts': [] })
    expect(findCircularDependencies(graph)).toHaveLength(0)
  })

  it('detects direct cycle', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': ['a.ts'] })
    const cycles = findCircularDependencies(graph)
    expect(cycles.length).toBeGreaterThan(0)
  })

  it('detects indirect cycle', () => {
    const graph = makeGraph({
      'a.ts': ['b.ts'],
      'b.ts': ['c.ts'],
      'c.ts': ['a.ts'],
    })
    const cycles = findCircularDependencies(graph)
    expect(cycles.length).toBeGreaterThan(0)
  })

  it('returns empty for single node', () => {
    const graph = makeGraph({ 'a.ts': [] })
    expect(findCircularDependencies(graph)).toHaveLength(0)
  })
})

// ============================================================================
// getDependencyDepth
// ============================================================================

describe('getDependencyDepth', () => {
  it('returns 0 for root nodes', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    expect(getDependencyDepth(graph, 'a.ts')).toBe(0)
  })

  it('returns depth for deeply imported file', () => {
    const graph = makeGraph({
      'a.ts': ['b.ts'],
      'b.ts': ['c.ts'],
      'c.ts': [],
    })
    // c is imported by b (depth 1), which is imported by a (depth 2)
    expect(getDependencyDepth(graph, 'c.ts')).toBe(2)
  })

  it('returns 0 for unknown file', () => {
    const graph = makeGraph({ 'a.ts': [] })
    expect(getDependencyDepth(graph, 'unknown.ts')).toBe(0)
  })
})

// ============================================================================
// getTransitiveDependencies
// ============================================================================

describe('getTransitiveDependencies', () => {
  it('returns direct dependencies', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [], 'c.ts': [] })
    const deps = getTransitiveDependencies(graph, 'a.ts')
    expect(deps.has('b.ts')).toBe(true)
    expect(deps.has('c.ts')).toBe(false)
  })

  it('returns transitive dependencies', () => {
    const graph = makeGraph({
      'a.ts': ['b.ts'],
      'b.ts': ['c.ts'],
      'c.ts': [],
    })
    const deps = getTransitiveDependencies(graph, 'a.ts')
    expect(deps.has('b.ts')).toBe(true)
    expect(deps.has('c.ts')).toBe(true)
  })

  it('returns empty for leaf nodes', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    expect(getTransitiveDependencies(graph, 'b.ts').size).toBe(0)
  })

  it('handles cycles without infinite loop', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': ['a.ts'] })
    const deps = getTransitiveDependencies(graph, 'a.ts')
    expect(deps.has('b.ts')).toBe(true)
  })
})

// ============================================================================
// getTransitiveDependents
// ============================================================================

describe('getTransitiveDependents', () => {
  it('returns direct dependents', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    const dependents = getTransitiveDependents(graph, 'b.ts')
    expect(dependents.has('a.ts')).toBe(true)
  })

  it('returns transitive dependents', () => {
    const graph = makeGraph({
      'a.ts': ['b.ts'],
      'b.ts': ['c.ts'],
      'c.ts': [],
    })
    const dependents = getTransitiveDependents(graph, 'c.ts')
    expect(dependents.has('b.ts')).toBe(true)
    expect(dependents.has('a.ts')).toBe(true)
  })

  it('returns empty for root nodes', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    expect(getTransitiveDependents(graph, 'a.ts').size).toBe(0)
  })
})

// ============================================================================
// getGraphStats
// ============================================================================

describe('getGraphStats', () => {
  it('returns correct stats for simple graph', () => {
    const graph = makeGraph({
      'a.ts': ['c.ts'],
      'b.ts': ['c.ts'],
      'c.ts': [],
    })
    const stats = getGraphStats(graph)
    expect(stats.totalFiles).toBe(3)
    expect(stats.totalEdges).toBe(2)
    expect(stats.roots).toBe(2) // a, b
    expect(stats.leaves).toBe(1) // c
    expect(stats.circularCount).toBe(0)
  })

  it('returns zeros for empty graph', () => {
    const graph = new Map<string, DependencyNode>()
    const stats = getGraphStats(graph)
    expect(stats.totalFiles).toBe(0)
    expect(stats.totalEdges).toBe(0)
    expect(stats.avgImports).toBe(0)
  })

  it('calculates avg imports', () => {
    const graph = makeGraph({
      'a.ts': ['b.ts', 'c.ts'],
      'b.ts': ['c.ts'],
      'c.ts': [],
    })
    const stats = getGraphStats(graph)
    expect(stats.avgImports).toBe(1) // (2+1+0)/3
    expect(stats.maxImports).toBe(2)
  })

  it('detects circular dependencies count', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': ['a.ts'] })
    const stats = getGraphStats(graph)
    expect(stats.circularCount).toBeGreaterThan(0)
  })
})
