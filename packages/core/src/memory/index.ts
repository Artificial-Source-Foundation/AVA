/**
 * Memory Module
 *
 * Long-term memory system with episodic, semantic, and procedural memories.
 *
 * @example
 * ```ts
 * import { createMemoryManager } from '@ava/core/memory'
 *
 * // Create memory manager
 * const memory = createMemoryManager({
 *   openAIApiKey: 'sk-...',
 *   consolidationInterval: 24 * 60 * 60 * 1000, // Daily
 * })
 *
 * // Initialize store
 * await memory.initialize()
 *
 * // Record session (episodic)
 * await memory.remember({
 *   sessionId: 'session-123',
 *   summary: 'Fixed authentication bug',
 *   decisions: ['Use JWT instead of sessions'],
 *   toolsUsed: ['read', 'write', 'bash'],
 *   outcome: 'success',
 *   durationMinutes: 45,
 * }, 'episodic')
 *
 * // Learn a fact (semantic)
 * await memory.remember({
 *   fact: 'The project uses TypeScript strict mode',
 *   source: 'tsconfig.json analysis',
 *   confidence: 0.95,
 * }, 'semantic')
 *
 * // Record a pattern (procedural)
 * await memory.remember({
 *   context: 'User asks to fix TypeScript errors',
 *   action: 'Run tsc --noEmit first to identify all errors',
 *   tools: ['bash', 'read'],
 *   success: true,
 * }, 'procedural')
 *
 * // Recall similar memories
 * const similar = await memory.recallSimilar('TypeScript configuration')
 * ```
 */

// Consolidation
export {
  ConsolidationEngine,
  createConsolidationEngine,
  DEFAULT_DECAY_RATE,
} from './consolidation.js'
// Embedding
export {
  CachingEmbedder,
  createCachingEmbedder,
  createMockEmbedder,
  createOpenAIEmbedder,
  EmbeddingError,
  MockEmbedder,
  OpenAIEmbedder,
} from './embedding.js'
// Episodic Memory
export { createEpisodicMemory, EpisodicMemoryManager } from './episodic.js'
// Manager
export {
  createMemoryManager,
  getMemoryManager,
  MemoryManager,
  type MemoryManagerOptions,
  setMemoryManager,
} from './manager.js'
// Procedural Memory
export { createProceduralMemory, ProceduralMemoryManager } from './procedural.js'
// Semantic Memory
export { createSemanticMemory, SemanticMemoryManager } from './semantic.js'
// Store
export { createVectorStore, SQLiteVectorStore } from './store.js'
// Types
export * from './types.js'
