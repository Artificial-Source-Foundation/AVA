/**
 * Delta9 Knowledge System
 *
 * Letta-style memory blocks for persistent learning.
 * Agents can store patterns, conventions, gotchas, and decisions.
 */

export { createKnowledgeStore } from './store.js'

export {
  type KnowledgeScope,
  type KnowledgeBlock,
  type KnowledgeStore,
  type KnowledgeFrontmatter,
  knowledgeFrontmatterSchema,
  SEED_BLOCKS,
  DEFAULT_LIMIT,
  MAX_LIMIT,
  LABEL_REGEX,
} from './types.js'

// Semantic search
export {
  SemanticIndex,
  createSemanticIndex,
  getSemanticIndex,
  resetSemanticIndex,
  createMockEmbeddingProvider,
  cosineSimilarity,
  normalizeVector,
  chunkText,
  describeSearchResults,
  type EmbeddingVector,
  type EmbeddingProvider,
  type IndexedDocument,
  type SemanticSearchResult,
  type SemanticIndexConfig,
  type IndexStats,
} from './semantic.js'
