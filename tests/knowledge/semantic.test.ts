/**
 * Tests for Semantic Memory Search
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

  describe('Vector Operations', () => {
    describe('cosineSimilarity', () => {
      it('should return 1 for identical vectors', () => {
        const v = [0.5, 0.5, 0.5, 0.5]
        expect(cosineSimilarity(v, v)).toBeCloseTo(1.0, 5)
      })

      it('should return 0 for orthogonal vectors', () => {
        const a = [1, 0, 0, 0]
        const b = [0, 1, 0, 0]
        expect(cosineSimilarity(a, b)).toBeCloseTo(0.0, 5)
      })

      it('should return -1 for opposite vectors', () => {
        const a = [1, 0, 0, 0]
        const b = [-1, 0, 0, 0]
        expect(cosineSimilarity(a, b)).toBeCloseTo(-1.0, 5)
      })

      it('should throw for mismatched dimensions', () => {
        const a = [1, 0, 0]
        const b = [1, 0]
        expect(() => cosineSimilarity(a, b)).toThrow('dimension mismatch')
      })

      it('should return 0 for zero vectors', () => {
        const a = [0, 0, 0, 0]
        const b = [1, 0, 0, 0]
        expect(cosineSimilarity(a, b)).toBe(0)
      })
    })

    describe('normalizeVector', () => {
      it('should normalize to unit length', () => {
        const v = [3, 4]
        const normalized = normalizeVector(v)

        const magnitude = Math.sqrt(
          normalized.reduce((sum, val) => sum + val * val, 0)
        )
        expect(magnitude).toBeCloseTo(1.0, 5)
      })

      it('should preserve direction', () => {
        const v = [3, 4]
        const normalized = normalizeVector(v)

        // Direction ratio should be preserved
        expect(normalized[0] / normalized[1]).toBeCloseTo(3 / 4, 5)
      })

      it('should handle zero vector', () => {
        const v = [0, 0, 0]
        const normalized = normalizeVector(v)

        expect(normalized).toEqual([0, 0, 0])
      })
    })
  })

  describe('Text Chunking', () => {
    describe('chunkText', () => {
      it('should return single chunk for short text', () => {
        const text = 'Short text'
        const chunks = chunkText(text, 100, 10)

        expect(chunks).toHaveLength(1)
        expect(chunks[0]).toBe(text)
      })

      it('should split long text into chunks', () => {
        const text = 'A'.repeat(500)
        const chunks = chunkText(text, 100, 20)

        expect(chunks.length).toBeGreaterThan(1)
        expect(chunks[0].length).toBeLessThanOrEqual(100)
      })

      it('should overlap chunks', () => {
        const text = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
        const chunks = chunkText(text, 10, 3)

        // With overlap, some characters should appear in multiple chunks
        if (chunks.length > 1) {
          const firstEnd = chunks[0].slice(-3)
          const secondStart = chunks[1].slice(0, 3)
          // The end of first chunk should overlap with start of next
          expect(chunks[0].length).toBeLessThanOrEqual(10)
        }
      })

      it('should try to break at sentence boundaries', () => {
        const text = 'First sentence. Second sentence. Third sentence.'
        const chunks = chunkText(text, 20, 5)

        // Should prefer breaking at periods
        expect(chunks.length).toBeGreaterThan(1)
      })

      it('should filter empty chunks', () => {
        const text = 'Test content'
        const chunks = chunkText(text, 100, 10)

        for (const chunk of chunks) {
          expect(chunk.length).toBeGreaterThan(0)
        }
      })
    })
  })

  describe('Mock Embedding Provider', () => {
    it('should create provider with specified dimension', () => {
      const provider = createMockEmbeddingProvider(256)
      expect(provider.dimension).toBe(256)
      expect(provider.name).toBe('mock')
    })

    it('should generate embeddings of correct dimension', async () => {
      const provider = createMockEmbeddingProvider(128)
      const embedding = await provider.embed('test text')

      expect(embedding.length).toBe(128)
    })

    it('should generate normalized embeddings', async () => {
      const provider = createMockEmbeddingProvider(64)
      const embedding = await provider.embed('test text')

      const magnitude = Math.sqrt(
        embedding.reduce((sum, val) => sum + val * val, 0)
      )
      expect(magnitude).toBeCloseTo(1.0, 3)
    })

    it('should be deterministic', async () => {
      const provider = createMockEmbeddingProvider(64)
      const e1 = await provider.embed('same text')
      const e2 = await provider.embed('same text')

      expect(e1).toEqual(e2)
    })

    it('should generate different embeddings for different text', async () => {
      const provider = createMockEmbeddingProvider(64)
      const e1 = await provider.embed('first text')
      const e2 = await provider.embed('second text')

      expect(e1).not.toEqual(e2)
    })

    it('should support batch embedding', async () => {
      const provider = createMockEmbeddingProvider(64)
      const embeddings = await provider.embedBatch!(['text1', 'text2', 'text3'])

      expect(embeddings.length).toBe(3)
      for (const e of embeddings) {
        expect(e.length).toBe(64)
      }
    })
  })

  describe('SemanticIndex', () => {
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

    describe('Indexing', () => {
      it('should index a knowledge block', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        const block = createMockBlock('test', 'This is test content')
        const chunks = await index.indexBlock(block)

        expect(chunks).toBeGreaterThan(0)
        expect(index.isBlockIndexed('project', 'test')).toBe(true)
      })

      it('should chunk large blocks', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          maxChunkSize: 50,
        })

        const block = createMockBlock('large', 'A'.repeat(200))
        const chunks = await index.indexBlock(block)

        expect(chunks).toBeGreaterThan(1)
      })

      it('should skip empty blocks', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        const block = createMockBlock('empty', '   ')
        const chunks = await index.indexBlock(block)

        expect(chunks).toBe(0)
      })

      it('should replace existing entries on re-index', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        const block1 = createMockBlock('test', 'Original content')
        await index.indexBlock(block1)

        const block2 = createMockBlock('test', 'Updated content')
        await index.indexBlock(block2)

        const stats = index.getStats()
        expect(stats.totalDocuments).toBe(1)
      })

      it('should index multiple blocks', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        const blocks = [
          createMockBlock('block1', 'First block content'),
          createMockBlock('block2', 'Second block content'),
          createMockBlock('block3', 'Third block content'),
        ]

        const totalChunks = await index.indexBlocks(blocks)

        expect(totalChunks).toBeGreaterThanOrEqual(3)
        expect(index.getStats().totalDocuments).toBe(3)
      })

      it('should remove block from index', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        const block = createMockBlock('test', 'Content')
        await index.indexBlock(block)

        expect(index.isBlockIndexed('project', 'test')).toBe(true)

        index.removeBlock('project', 'test')

        expect(index.isBlockIndexed('project', 'test')).toBe(false)
      })

      it('should clear all indexed documents', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        await index.indexBlocks([
          createMockBlock('b1', 'Content 1'),
          createMockBlock('b2', 'Content 2'),
        ])

        index.clear()

        expect(index.getStats().totalDocuments).toBe(0)
      })
    })

    describe('Search', () => {
      it('should find similar documents', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
        })

        await index.indexBlocks([
          createMockBlock('patterns', 'React component patterns with hooks'),
          createMockBlock('conventions', 'Code style conventions for TypeScript'),
          createMockBlock('gotchas', 'Common mistakes and gotchas'),
        ])

        const results = await index.search('React hooks')

        expect(results.length).toBeGreaterThan(0)
        expect(results[0].score).toBeGreaterThan(0)
        expect(results[0].rank).toBe(1)
      })

      it('should filter by scope', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
        })

        await index.indexBlocks([
          createMockBlock('project-only', 'Project specific content', 'project'),
          createMockBlock('global-only', 'Global content', 'global'),
        ])

        const projectResults = await index.search('content', { scope: 'project' })
        const globalResults = await index.search('content', { scope: 'global' })

        expect(projectResults.every((r) => r.document.source.scope === 'project')).toBe(true)
        expect(globalResults.every((r) => r.document.source.scope === 'global')).toBe(true)
      })

      it('should filter by category', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
        })

        await index.indexBlocks([
          createMockBlock('p1', 'Pattern content', 'project', 'patterns'),
          createMockBlock('c1', 'Convention content', 'project', 'conventions'),
        ])

        const results = await index.search('content', { category: 'patterns' })

        expect(results.every((r) => r.document.source.category === 'patterns')).toBe(true)
      })

      it('should respect similarity threshold', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.9,
        })

        await index.indexBlock(createMockBlock('test', 'Some content'))

        const results = await index.search('completely different')

        // High threshold should filter out low-similarity results
        expect(results.every((r) => r.score >= 0.9)).toBe(true)
      })

      it('should respect max results limit', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
          maxResults: 2,
        })

        await index.indexBlocks([
          createMockBlock('b1', 'Content one'),
          createMockBlock('b2', 'Content two'),
          createMockBlock('b3', 'Content three'),
          createMockBlock('b4', 'Content four'),
        ])

        const results = await index.search('content')

        expect(results.length).toBeLessThanOrEqual(2)
      })

      it('should sort by score descending', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
        })

        await index.indexBlocks([
          createMockBlock('b1', 'React component'),
          createMockBlock('b2', 'React hooks pattern'),
          createMockBlock('b3', 'TypeScript types'),
        ])

        const results = await index.search('React')

        for (let i = 0; i < results.length - 1; i++) {
          expect(results[i].score).toBeGreaterThanOrEqual(results[i + 1].score)
        }
      })
    })

    describe('findSimilarBlocks', () => {
      it('should dedupe results by source block', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          maxChunkSize: 20,
          minSimilarity: 0.1,
        })

        // This will create multiple chunks from the same block
        await index.indexBlock(
          createMockBlock('chunked', 'A'.repeat(100))
        )

        const results = await index.findSimilarBlocks('A')

        // Should return 1 block even though there are multiple chunks
        expect(results.length).toBe(1)
        expect(results[0].label).toBe('chunked')
      })

      it('should track matched chunks per block', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          maxChunkSize: 30,
          minSimilarity: 0.1,
        })

        await index.indexBlock(
          createMockBlock('multi', 'A'.repeat(100))
        )

        const results = await index.findSimilarBlocks('A')

        expect(results[0].matchedChunks).toBeGreaterThanOrEqual(1)
      })

      it('should return best score per block', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          minSimilarity: 0.1,
        })

        await index.indexBlocks([
          createMockBlock('b1', 'First content'),
          createMockBlock('b2', 'Second content'),
        ])

        const results = await index.findSimilarBlocks('first')

        expect(results[0].bestScore).toBeGreaterThan(0)
      })
    })

    describe('Statistics', () => {
      it('should track total documents', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        await index.indexBlocks([
          createMockBlock('b1', 'Content 1'),
          createMockBlock('b2', 'Content 2'),
        ])

        expect(index.getStats().totalDocuments).toBe(2)
      })

      it('should track chunks separately from documents', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
          maxChunkSize: 30,
        })

        await index.indexBlock(
          createMockBlock('large', 'A'.repeat(100))
        )

        const stats = index.getStats()
        expect(stats.totalDocuments).toBe(1)
        expect(stats.totalChunks).toBeGreaterThan(1)
      })

      it('should track by scope', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        await index.indexBlocks([
          createMockBlock('p1', 'Content', 'project'),
          createMockBlock('p2', 'Content', 'project'),
          createMockBlock('g1', 'Content', 'global'),
        ])

        const stats = index.getStats()
        expect(stats.byScope.project).toBe(2)
        expect(stats.byScope.global).toBe(1)
      })

      it('should track by category', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        await index.indexBlocks([
          createMockBlock('p1', 'Content', 'project', 'patterns'),
          createMockBlock('c1', 'Content', 'project', 'conventions'),
          createMockBlock('c2', 'Content', 'project', 'conventions'),
        ])

        const stats = index.getStats()
        expect(stats.byCategory.patterns).toBe(1)
        expect(stats.byCategory.conventions).toBe(2)
      })

      it('should track provider info', () => {
        const provider = createMockEmbeddingProvider(256)
        const index = createSemanticIndex({ provider })

        const stats = index.getStats()
        expect(stats.embeddingDimension).toBe(256)
        expect(stats.providerName).toBe('mock')
      })

      it('should track last indexed time', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        const before = new Date()
        await index.indexBlock(createMockBlock('test', 'Content'))
        const after = new Date()

        const stats = index.getStats()
        expect(stats.lastIndexedAt).toBeDefined()
        expect(stats.lastIndexedAt!.getTime()).toBeGreaterThanOrEqual(before.getTime())
        expect(stats.lastIndexedAt!.getTime()).toBeLessThanOrEqual(after.getTime())
      })

      it('should list indexed blocks', async () => {
        const index = createSemanticIndex({
          provider: createMockEmbeddingProvider(),
        })

        await index.indexBlocks([
          createMockBlock('b1', 'Content', 'project'),
          createMockBlock('b2', 'Content', 'global'),
        ])

        const blocks = index.getIndexedBlocks()
        expect(blocks).toHaveLength(2)
        expect(blocks).toContainEqual({ scope: 'project', label: 'b1' })
        expect(blocks).toContainEqual({ scope: 'global', label: 'b2' })
      })
    })
  })

  describe('Singleton', () => {
    it('should return same instance', () => {
      const index1 = getSemanticIndex({ provider: createMockEmbeddingProvider() })
      const index2 = getSemanticIndex()

      expect(index1).toBe(index2)
    })

    it('should reset instance', () => {
      const index1 = getSemanticIndex({ provider: createMockEmbeddingProvider() })
      resetSemanticIndex()
      const index2 = getSemanticIndex({ provider: createMockEmbeddingProvider() })

      expect(index1).not.toBe(index2)
    })
  })

  describe('describeSearchResults', () => {
    it('should describe no results', () => {
      const description = describeSearchResults([])
      expect(description).toContain('No matching results')
    })

    it('should describe results with scores', () => {
      const results = [
        {
          document: {
            id: 'project:test:0',
            source: { scope: 'project' as const, label: 'test', category: 'patterns' },
            text: 'Test content',
            embedding: [0, 0, 0],
            indexedAt: new Date(),
          },
          score: 0.85,
          rank: 1,
        },
      ]

      const description = describeSearchResults(results)

      expect(description).toContain('1 matching result')
      expect(description).toContain('project:test')
      expect(description).toContain('85.0%')
    })

    it('should show chunk info for chunked documents', () => {
      const results = [
        {
          document: {
            id: 'project:test:1',
            source: { scope: 'project' as const, label: 'test', category: 'patterns' },
            text: 'Chunk content',
            embedding: [0, 0, 0],
            chunkIndex: 1,
            totalChunks: 3,
            indexedAt: new Date(),
          },
          score: 0.75,
          rank: 1,
        },
      ]

      const description = describeSearchResults(results)

      expect(description).toContain('chunk 2/3')
    })
  })
})
