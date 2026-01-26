/**
 * Tests for Semantic Memory Search
 *
 * Consolidated tests using representative sampling.
 */

import { describe, it, expect, beforeEach } from 'vitest'
import {
  SemanticIndex,
  createSemanticIndex,
  getSemanticIndex,
  resetSemanticIndex,
  createMockEmbeddingProvider,
  cosineSimilarity,
  normalizeVector,
  chunkText,
  describeSearchResults,
  type KnowledgeBlock,
} from '../../src/knowledge/index.js'

describe('Semantic Memory Search', () => {
  beforeEach(() => {
    resetSemanticIndex()
  })

  const createMockBlock = (
    label: string,
    value: string,
    scope: 'project' | 'global' = 'project',
    category: 'patterns' | 'conventions' | 'gotchas' | 'decisions' | 'custom' = 'patterns'
  ): KnowledgeBlock => ({
    scope,
    label,
    description: `Test block: ${label}`,
    limit: 10000,
    readOnly: false,
    category,
    value,
    filePath: `/test/${label}.md`,
    lastModified: new Date(),
    charCount: value.length,
  })

  describe('Vector Operations', () => {
    it('cosineSimilarity calculates correctly', () => {
      const v = [0.5, 0.5, 0.5, 0.5]
      expect(cosineSimilarity(v, v)).toBeCloseTo(1.0, 5) // identical

      expect(cosineSimilarity([1, 0, 0, 0], [0, 1, 0, 0])).toBeCloseTo(0.0, 5) // orthogonal
      expect(cosineSimilarity([1, 0, 0, 0], [-1, 0, 0, 0])).toBeCloseTo(-1.0, 5) // opposite
      expect(cosineSimilarity([0, 0, 0, 0], [1, 0, 0, 0])).toBe(0) // zero vector
      expect(() => cosineSimilarity([1, 0, 0], [1, 0])).toThrow('dimension mismatch')
    })

    it('normalizeVector normalizes to unit length', () => {
      const normalized = normalizeVector([3, 4])
      const magnitude = Math.sqrt(normalized.reduce((sum, val) => sum + val * val, 0))
      expect(magnitude).toBeCloseTo(1.0, 5)
      expect(normalized[0] / normalized[1]).toBeCloseTo(3 / 4, 5) // preserves direction
      expect(normalizeVector([0, 0, 0])).toEqual([0, 0, 0]) // handles zero
    })
  })

  describe('Text Chunking', () => {
    it('chunks text correctly', () => {
      // Short text - single chunk
      expect(chunkText('Short text', 100, 10)).toHaveLength(1)

      // Long text - multiple chunks
      const chunks = chunkText('A'.repeat(500), 100, 20)
      expect(chunks.length).toBeGreaterThan(1)
      expect(chunks[0].length).toBeLessThanOrEqual(100)

      // Filters empty chunks
      for (const chunk of chunkText('Test content', 100, 10)) {
        expect(chunk.length).toBeGreaterThan(0)
      }
    })
  })

  describe('Mock Embedding Provider', () => {
    it('creates provider with correct behavior', async () => {
      const provider = createMockEmbeddingProvider(128)
      expect(provider.dimension).toBe(128)
      expect(provider.name).toBe('mock')

      // Correct dimension and normalized
      const embedding = await provider.embed('test text')
      expect(embedding.length).toBe(128)
      const magnitude = Math.sqrt(embedding.reduce((sum, val) => sum + val * val, 0))
      expect(magnitude).toBeCloseTo(1.0, 3)

      // Deterministic
      const e1 = await provider.embed('same text')
      const e2 = await provider.embed('same text')
      expect(e1).toEqual(e2)

      // Different text = different embeddings
      expect(await provider.embed('first')).not.toEqual(await provider.embed('second'))

      // Batch support
      const batch = await provider.embedBatch!(['text1', 'text2', 'text3'])
      expect(batch.length).toBe(3)
    })
  })

  describe('SemanticIndex', () => {
    describe('Indexing', () => {
      it('indexes and manages blocks', async () => {
        const index = createSemanticIndex({ provider: createMockEmbeddingProvider() })

        // Index a block
        const block = createMockBlock('test', 'This is test content')
        const chunks = await index.indexBlock(block)
        expect(chunks).toBeGreaterThan(0)
        expect(index.isBlockIndexed('project', 'test')).toBe(true)

        // Large blocks are chunked
        const index2 = createSemanticIndex({ provider: createMockEmbeddingProvider(), maxChunkSize: 50 })
        const largeChunks = await index2.indexBlock(createMockBlock('large', 'A'.repeat(200)))
        expect(largeChunks).toBeGreaterThan(1)

        // Empty blocks skipped
        const emptyChunks = await index.indexBlock(createMockBlock('empty', '   '))
        expect(emptyChunks).toBe(0)

        // Re-indexing replaces
        await index.indexBlock(createMockBlock('test', 'Updated content'))
        expect(index.getStats().totalDocuments).toBe(1)

        // Remove block
        index.removeBlock('project', 'test')
        expect(index.isBlockIndexed('project', 'test')).toBe(false)

        // Clear all
        await index.indexBlocks([createMockBlock('b1', 'Content 1'), createMockBlock('b2', 'Content 2')])
        index.clear()
        expect(index.getStats().totalDocuments).toBe(0)
      })
    })

    describe('Search', () => {
      it('searches and filters correctly', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
          maxResults: 2,
        })

        await index.indexBlocks([
          createMockBlock('patterns', 'React component patterns with hooks', 'project', 'patterns'),
          createMockBlock('conventions', 'Code style conventions for TypeScript', 'project', 'conventions'),
          createMockBlock('global-only', 'Global content', 'global', 'patterns'),
        ])

        // Basic search
        const results = await index.search('React hooks')
        expect(results.length).toBeGreaterThan(0)
        expect(results[0].score).toBeGreaterThan(0)
        expect(results[0].rank).toBe(1)

        // Filter by scope
        const projectResults = await index.search('content', { scope: 'project' })
        expect(projectResults.every((r) => r.document.source.scope === 'project')).toBe(true)

        // Filter by category
        const patternResults = await index.search('content', { category: 'patterns' })
        expect(patternResults.every((r) => r.document.source.category === 'patterns')).toBe(true)

        // Respects max results
        expect(results.length).toBeLessThanOrEqual(2)

        // Sorted by score
        for (let i = 0; i < results.length - 1; i++) {
          expect(results[i].score).toBeGreaterThanOrEqual(results[i + 1].score)
        }
      })
    })

    describe('findSimilarBlocks', () => {
      it('dedupes and tracks chunks per block', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          maxChunkSize: 20,
          minSimilarity: 0.1,
        })

        await index.indexBlock(createMockBlock('chunked', 'A'.repeat(100)))
        const results = await index.findSimilarBlocks('A')

        expect(results.length).toBe(1)
        expect(results[0].label).toBe('chunked')
        expect(results[0].matchedChunks).toBeGreaterThanOrEqual(1)
        expect(results[0].bestScore).toBeGreaterThan(0)
      })
    })

    describe('Statistics', () => {
      it('tracks statistics correctly', async () => {
        const provider = createMockEmbeddingProvider(256)
        const index = createSemanticIndex({ provider, maxChunkSize: 30 })

        const before = new Date()
        await index.indexBlocks([
          createMockBlock('p1', 'Content', 'project', 'patterns'),
          createMockBlock('p2', 'Content', 'project', 'conventions'),
          createMockBlock('g1', 'Content', 'global', 'patterns'),
        ])
        const after = new Date()

        const stats = index.getStats()
        expect(stats.totalDocuments).toBe(3)
        expect(stats.byScope.project).toBe(2)
        expect(stats.byScope.global).toBe(1)
        expect(stats.byCategory.patterns).toBe(2)
        expect(stats.byCategory.conventions).toBe(1)
        expect(stats.embeddingDimension).toBe(256)
        expect(stats.providerName).toBe('mock')
        expect(stats.lastIndexedAt!.getTime()).toBeGreaterThanOrEqual(before.getTime())

        const blocks = index.getIndexedBlocks()
        expect(blocks).toHaveLength(3)
      })
    })
  })

  describe('Singleton', () => {
    it('manages singleton correctly', () => {
      const index1 = getSemanticIndex({ provider: createMockEmbeddingProvider() })
      const index2 = getSemanticIndex()
      expect(index1).toBe(index2)

      resetSemanticIndex()
      const index3 = getSemanticIndex({ provider: createMockEmbeddingProvider() })
      expect(index1).not.toBe(index3)
    })
  })

  describe('describeSearchResults', () => {
    it('describes results correctly', () => {
      expect(describeSearchResults([])).toContain('No matching results')

      const results = [{
        document: {
          id: 'project:test:0',
          source: { scope: 'project' as const, label: 'test', category: 'patterns' },
          text: 'Test content',
          embedding: [0, 0, 0],
          indexedAt: new Date(),
        },
        score: 0.85,
        rank: 1,
      }]

      const description = describeSearchResults(results)
      expect(description).toContain('1 matching result')
      expect(description).toContain('project:test')
      expect(description).toContain('85.0%')
    })
  })
})
