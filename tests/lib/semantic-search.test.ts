/**
 * Tests for Delta9 Semantic Search
 */

import { describe, it, expect } from 'vitest'
import {
  semanticSearch,
  searchMemoryBlocks,
  rerankResults,
  filterByScore,
  topResults,
  parseQuery,
  normalizeText,
  tokenize,
  extractTags,
  DEFAULT_SEARCH_CONFIG,
  type SearchableItem,
} from '../../src/lib/semantic-search.js'

describe('semanticSearch', () => {
  const items: SearchableItem[] = [
    {
      id: '1',
      label: 'patterns',
      description: 'Successful patterns and approaches',
      content: 'React hooks pattern. Use useState for state management. Testing with Vitest.',
      tags: ['react', 'hooks', 'testing'],
    },
    {
      id: '2',
      label: 'failures',
      description: 'Things that failed',
      content: 'Mutation testing failed. Performance issues with large datasets. Memory leaks in worker.',
      tags: ['failures', 'performance'],
    },
    {
      id: '3',
      label: 'context',
      description: 'Project context',
      content: 'TypeScript strict mode. ESLint with strict rules. Vitest for testing.',
      tags: ['typescript', 'eslint', 'vitest'],
    },
  ]

  it('finds items matching query', () => {
    const results = semanticSearch(items, 'testing')

    expect(results.length).toBeGreaterThan(0)
    // Should find patterns (Vitest) and context (Vitest for testing)
    expect(results.some((r) => r.item.label === 'patterns')).toBe(true)
    expect(results.some((r) => r.item.label === 'context')).toBe(true)
  })

  it('ranks better matches higher', () => {
    const results = semanticSearch(items, 'React hooks')

    expect(results.length).toBeGreaterThan(0)
    // patterns should be ranked higher (has exact match for react hooks)
    expect(results[0].item.label).toBe('patterns')
  })

  it('returns empty for no matches', () => {
    const results = semanticSearch(items, 'xyznonexistent')
    expect(results).toHaveLength(0)
  })

  it('respects maxResults', () => {
    const results = semanticSearch(items, 'pattern', { maxResults: 1 })
    expect(results.length).toBeLessThanOrEqual(1)
  })

  it('respects minScore', () => {
    const results = semanticSearch(items, 'testing', { minScore: 0.5 })
    results.forEach((r) => {
      expect(r.score).toBeGreaterThanOrEqual(0.5)
    })
  })

  it('includes score breakdown', () => {
    const results = semanticSearch(items, 'testing')

    expect(results.length).toBeGreaterThan(0)
    expect(results[0].breakdown).toBeDefined()
    expect(results[0].breakdown.labelScore).toBeDefined()
    expect(results[0].breakdown.contentScore).toBeDefined()
  })

  it('tracks matched terms', () => {
    const results = semanticSearch(items, 'react hooks testing')

    const patternResult = results.find((r) => r.item.label === 'patterns')
    expect(patternResult?.matchedTerms).toContain('react')
    expect(patternResult?.matchedTerms).toContain('hooks')
    expect(patternResult?.matchedTerms).toContain('testing')
  })

  it('applies keyword boost for exact matches', () => {
    const results = semanticSearch(items, 'useState', { keywordBoost: 2.0 })

    const patternResult = results.find((r) => r.item.label === 'patterns')
    expect(patternResult?.breakdown.keywordBonus).toBe(2.0)
  })
})

describe('searchMemoryBlocks', () => {
  const blocks = [
    {
      label: 'patterns',
      description: 'Code patterns',
      content: '# Patterns\n\n## React Hooks\n\nUse **useState** for state.',
    },
    {
      label: 'failures',
      description: 'Past failures',
      content: '# Failures\n\n## Performance Issues\n\nAvoid **large arrays** in render.',
    },
  ]

  it('searches memory blocks with extracted tags', () => {
    const results = searchMemoryBlocks(blocks, 'react')

    expect(results.length).toBeGreaterThan(0)
    expect(results[0].item.label).toBe('patterns')
  })

  it('includes original block reference', () => {
    const results = searchMemoryBlocks(blocks, 'patterns')

    expect(results[0].original).toBeDefined()
    expect(results[0].original?.label).toBe('patterns')
  })
})

describe('rerankResults', () => {
  it('reranks with custom scorer', () => {
    const items: SearchableItem[] = [
      { id: '1', label: 'a', content: 'content a' },
      { id: '2', label: 'b', content: 'content b longer' },
    ]

    const results = semanticSearch(items, 'content')

    // Rerank by content length
    const reranked = rerankResults(results, (r) => r.item.content.length / 100)

    expect(reranked[0].item.id).toBe('2') // Longer content first
  })
})

describe('filterByScore', () => {
  it('filters results below threshold', () => {
    const items: SearchableItem[] = [
      { id: '1', label: 'match', content: 'exact match here' },
      { id: '2', label: 'partial', content: 'somewhat related' },
    ]

    const results = semanticSearch(items, 'exact match', { minScore: 0 })
    const filtered = filterByScore(results, 0.3)

    filtered.forEach((r) => {
      expect(r.score).toBeGreaterThanOrEqual(0.3)
    })
  })
})

describe('topResults', () => {
  it('limits results to N', () => {
    const items: SearchableItem[] = Array.from({ length: 10 }, (_, i) => ({
      id: String(i),
      label: `item-${i}`,
      content: 'common content here',
    }))

    const results = semanticSearch(items, 'common')
    const top3 = topResults(results, 3)

    expect(top3).toHaveLength(3)
  })
})

describe('parseQuery', () => {
  it('extracts scope filter', () => {
    const parsed = parseQuery('scope:project bug fixes')

    expect(parsed.scope).toBe('project')
    expect(parsed.query).toBe('bug fixes')
  })

  it('extracts label filter', () => {
    const parsed = parseQuery('label:failures memory issues')

    expect(parsed.label).toBe('failures')
    expect(parsed.query).toBe('memory issues')
  })

  it('handles multiple filters', () => {
    const parsed = parseQuery('scope:global label:patterns best practices')

    expect(parsed.scope).toBe('global')
    expect(parsed.label).toBe('patterns')
    expect(parsed.query).toBe('best practices')
  })

  it('preserves raw query', () => {
    const parsed = parseQuery('scope:project test query')

    expect(parsed.raw).toBe('scope:project test query')
  })

  it('handles query without filters', () => {
    const parsed = parseQuery('simple search query')

    expect(parsed.scope).toBeUndefined()
    expect(parsed.label).toBeUndefined()
    expect(parsed.query).toBe('simple search query')
  })
})

describe('normalizeText', () => {
  it('lowercases text', () => {
    expect(normalizeText('Hello World')).toBe('hello world')
  })

  it('removes punctuation', () => {
    expect(normalizeText('hello, world!')).toBe('hello world')
  })

  it('collapses whitespace', () => {
    expect(normalizeText('hello   world')).toBe('hello world')
  })

  it('trims text', () => {
    expect(normalizeText('  hello  ')).toBe('hello')
  })
})

describe('tokenize', () => {
  it('splits on whitespace', () => {
    const tokens = tokenize('hello world test')
    expect(tokens).toContain('hello')
    expect(tokens).toContain('world')
    expect(tokens).toContain('test')
  })

  it('filters short terms', () => {
    const tokens = tokenize('a ab abc abcd')
    expect(tokens).not.toContain('a')
    expect(tokens).toContain('ab')
    expect(tokens).toContain('abc')
    expect(tokens).toContain('abcd')
  })

  it('filters stop words', () => {
    const tokens = tokenize('the quick brown fox')
    expect(tokens).not.toContain('the')
    expect(tokens).toContain('quick')
    expect(tokens).toContain('brown')
    expect(tokens).toContain('fox')
  })
})

describe('extractTags', () => {
  it('extracts markdown headers', () => {
    const tags = extractTags('# Main Header\n\n## Sub Header\n\nContent')

    expect(tags).toContain('main header')
    expect(tags).toContain('sub header')
  })

  it('extracts bold text', () => {
    const tags = extractTags('Use **React Hooks** for **state management**')

    expect(tags).toContain('react hooks')
    expect(tags).toContain('state management')
  })

  it('deduplicates tags', () => {
    const tags = extractTags('## React\n\n## React\n\n**React**')

    const reactCount = tags.filter((t) => t === 'react').length
    expect(reactCount).toBe(1)
  })

  it('filters long bold text', () => {
    const tags = extractTags('**This is a very long bold text that should not be extracted as a tag because it is too long**')

    expect(tags).not.toContain(
      'this is a very long bold text that should not be extracted as a tag because it is too long'
    )
  })
})

describe('DEFAULT_SEARCH_CONFIG', () => {
  it('has reasonable defaults', () => {
    expect(DEFAULT_SEARCH_CONFIG.labelWeight).toBeGreaterThan(0)
    expect(DEFAULT_SEARCH_CONFIG.contentWeight).toBeGreaterThan(0)
    expect(DEFAULT_SEARCH_CONFIG.labelWeight + DEFAULT_SEARCH_CONFIG.contentWeight).toBe(1)
    expect(DEFAULT_SEARCH_CONFIG.keywordBoost).toBeGreaterThanOrEqual(1)
    expect(DEFAULT_SEARCH_CONFIG.minScore).toBeLessThan(1)
    expect(DEFAULT_SEARCH_CONFIG.maxResults).toBeGreaterThan(0)
  })
})
