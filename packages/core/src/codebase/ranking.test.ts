/**
 * File Ranking Tests
 */

import { describe, expect, it } from 'vitest'
import {
  calculatePageRank,
  calculateRelevanceScore,
  extractKeywords,
  sortByRank,
  sortByScore,
} from './ranking.js'
import type { DependencyNode } from './types.js'

// ============================================================================
// Helper
// ============================================================================

function makeGraph(edges: Record<string, string[]>): Map<string, DependencyNode> {
  const graph = new Map<string, DependencyNode>()
  // Initialize nodes
  for (const file of Object.keys(edges)) {
    graph.set(file, { file, imports: [], importedBy: [], rank: 0 })
  }
  // Build edges
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
// calculatePageRank
// ============================================================================

describe('calculatePageRank', () => {
  it('returns empty map for empty graph', () => {
    const result = calculatePageRank(new Map())
    expect(result.size).toBe(0)
  })

  it('assigns equal rank to isolated nodes', () => {
    const graph = makeGraph({ 'a.ts': [], 'b.ts': [], 'c.ts': [] })
    const ranks = calculatePageRank(graph)
    const values = Array.from(ranks.values())
    expect(values[0]).toBeCloseTo(values[1], 5)
    expect(values[1]).toBeCloseTo(values[2], 5)
  })

  it('gives higher rank to heavily imported file', () => {
    // a, b, c all import utils
    const graph = makeGraph({
      'a.ts': ['utils.ts'],
      'b.ts': ['utils.ts'],
      'c.ts': ['utils.ts'],
      'utils.ts': [],
    })
    const ranks = calculatePageRank(graph)
    expect(ranks.get('utils.ts')!).toBeGreaterThan(ranks.get('a.ts')!)
  })

  it('respects custom damping factor', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    const ranks1 = calculatePageRank(graph, { damping: 0.5 })
    const ranks2 = calculatePageRank(graph, { damping: 0.85 })
    // Different damping produces different rank distributions
    expect(ranks1.get('b.ts')!).not.toBeCloseTo(ranks2.get('b.ts')!, 5)
  })

  it('converges with tolerance', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': ['a.ts'] })
    const ranks = calculatePageRank(graph, { tolerance: 0.1 })
    expect(ranks.size).toBe(2)
  })

  it('updates node.rank in graph', () => {
    const graph = makeGraph({ 'a.ts': ['b.ts'], 'b.ts': [] })
    calculatePageRank(graph)
    expect(graph.get('b.ts')!.rank).toBeGreaterThan(0)
  })

  it('all ranks are positive', () => {
    const graph = makeGraph({
      'a.ts': ['c.ts'],
      'b.ts': ['c.ts'],
      'c.ts': ['d.ts'],
      'd.ts': [],
    })
    const ranks = calculatePageRank(graph)
    for (const rank of ranks.values()) {
      expect(rank).toBeGreaterThan(0)
    }
  })
})

// ============================================================================
// calculateRelevanceScore
// ============================================================================

describe('calculateRelevanceScore', () => {
  const baseFile = {
    path: '/project/src/auth/login.ts',
    relativePath: 'src/auth/login.ts',
    mtime: Date.now(),
    symbols: [{ name: 'authenticate' }, { name: 'LoginForm' }],
  }

  it('returns score between 0 and 1', () => {
    const { score } = calculateRelevanceScore(baseFile, 0.01, ['auth'])
    expect(score).toBeGreaterThanOrEqual(0)
    expect(score).toBeLessThanOrEqual(1)
  })

  it('higher pageRank increases score', () => {
    const low = calculateRelevanceScore(baseFile, 0.001, [])
    const high = calculateRelevanceScore(baseFile, 0.1, [])
    expect(high.score).toBeGreaterThan(low.score)
  })

  it('keyword match increases score', () => {
    const noMatch = calculateRelevanceScore(baseFile, 0.01, ['database'])
    const match = calculateRelevanceScore(baseFile, 0.01, ['auth'])
    expect(match.score).toBeGreaterThan(noMatch.score)
  })

  it('symbol match contributes to score', () => {
    const noMatch = calculateRelevanceScore(baseFile, 0.01, ['foobar'])
    const match = calculateRelevanceScore(baseFile, 0.01, ['authenticate'])
    expect(match.score).toBeGreaterThan(noMatch.score)
  })

  it('recent files score higher', () => {
    const recent = { ...baseFile, mtime: Date.now() }
    const old = { ...baseFile, mtime: Date.now() - 30 * 24 * 60 * 60 * 1000 }
    const recentScore = calculateRelevanceScore(recent, 0.01, [])
    const oldScore = calculateRelevanceScore(old, 0.01, [])
    expect(recentScore.score).toBeGreaterThan(oldScore.score)
  })

  it('includes reasons when scores are high', () => {
    const file = { ...baseFile, mtime: Date.now() }
    const { reasons } = calculateRelevanceScore(file, 0.1, ['auth', 'login'])
    expect(reasons.length).toBeGreaterThan(0)
  })

  it('respects custom weights', () => {
    const result = calculateRelevanceScore(baseFile, 0.1, ['auth'], {
      pageRankWeight: 0,
      keywordWeight: 1,
      recencyWeight: 0,
    })
    // All weight on keyword match
    expect(result.score).toBeGreaterThan(0)
  })
})

// ============================================================================
// extractKeywords
// ============================================================================

describe('extractKeywords', () => {
  it('extracts meaningful words', () => {
    const keywords = extractKeywords('Fix the authentication bug in login page')
    expect(keywords).toContain('fix')
    expect(keywords).toContain('authentication')
    expect(keywords).toContain('bug')
    expect(keywords).toContain('login')
    expect(keywords).toContain('page')
  })

  it('filters stop words', () => {
    const keywords = extractKeywords('add the new feature to the system')
    expect(keywords).not.toContain('the')
    expect(keywords).not.toContain('to')
    expect(keywords).toContain('add')
    expect(keywords).toContain('feature')
    expect(keywords).toContain('system')
  })

  it('preserves technical terms', () => {
    const keywords = extractKeywords('Fix the API and UI for CLI')
    expect(keywords).toContain('api')
    expect(keywords).toContain('cli')
  })

  it('extracts camelCase terms', () => {
    const keywords = extractKeywords('Update the handleUserAuth method')
    expect(keywords.some((k) => k.includes('handleuserauth'))).toBe(true)
  })

  it('returns empty for empty string', () => {
    const keywords = extractKeywords('')
    expect(keywords).toHaveLength(0)
  })

  it('filters pure numbers', () => {
    const keywords = extractKeywords('Fix bug 123 in version 456')
    expect(keywords).not.toContain('123')
    expect(keywords).not.toContain('456')
  })
})

// ============================================================================
// Sorting Utilities
// ============================================================================

describe('sortByRank', () => {
  it('sorts files by rank highest first', () => {
    const files = [{ path: 'a.ts' }, { path: 'b.ts' }, { path: 'c.ts' }]
    const ranks = new Map([
      ['a.ts', 0.1],
      ['b.ts', 0.5],
      ['c.ts', 0.3],
    ])
    const sorted = sortByRank(files, ranks)
    expect(sorted[0].path).toBe('b.ts')
    expect(sorted[1].path).toBe('c.ts')
    expect(sorted[2].path).toBe('a.ts')
  })

  it('does not mutate original array', () => {
    const files = [{ path: 'a.ts' }, { path: 'b.ts' }]
    const ranks = new Map([
      ['a.ts', 0.5],
      ['b.ts', 0.1],
    ])
    const sorted = sortByRank(files, ranks)
    expect(sorted).not.toBe(files)
  })

  it('treats missing ranks as 0', () => {
    const files = [{ path: 'a.ts' }, { path: 'b.ts' }]
    const ranks = new Map([['a.ts', 0.5]])
    const sorted = sortByRank(files, ranks)
    expect(sorted[0].path).toBe('a.ts')
  })
})

describe('sortByScore', () => {
  it('sorts files by score highest first', () => {
    const files = [
      { path: 'a.ts', score: 0.1 },
      { path: 'b.ts', score: 0.9 },
      { path: 'c.ts', score: 0.5 },
    ]
    const sorted = sortByScore(files)
    expect(sorted[0].path).toBe('b.ts')
    expect(sorted[2].path).toBe('a.ts')
  })

  it('does not mutate original array', () => {
    const files = [
      { path: 'a.ts', score: 0.1 },
      { path: 'b.ts', score: 0.9 },
    ]
    const sorted = sortByScore(files)
    expect(sorted).not.toBe(files)
  })
})
