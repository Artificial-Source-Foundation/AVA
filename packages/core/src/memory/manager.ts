/**
 * Memory Manager
 *
 * Unified interface for all memory subsystems.
 * Coordinates episodic, semantic, and procedural memories.
 */

import { ConsolidationEngine, DEFAULT_DECAY_RATE } from './consolidation.js'
import { CachingEmbedder, createOpenAIEmbedder } from './embedding.js'
import { EpisodicMemoryManager } from './episodic.js'
import { ProceduralMemoryManager } from './procedural.js'
import { SemanticMemoryManager } from './semantic.js'
import { SQLiteVectorStore } from './store.js'
import type {
  ConsolidationResult,
  CreateEpisodicMemoryInput,
  CreateProceduralMemoryInput,
  CreateSemanticMemoryInput,
  Embedder,
  IMemoryManager,
  MemoryEntry,
  MemoryEvent,
  MemoryEventListener,
  MemoryId,
  MemoryQuery,
  MemoryType,
  SimilarityResult,
  VectorStore,
} from './types.js'

// ============================================================================
// Memory Manager Options
// ============================================================================

/** Options for creating a memory manager */
export interface MemoryManagerOptions {
  /** Custom embedder (defaults to OpenAI with caching) */
  embedder?: Embedder
  /** Custom vector store (defaults to SQLite) */
  store?: VectorStore
  /** Decay rate for consolidation */
  decayRate?: number
  /** Auto-consolidation interval in milliseconds (0 = disabled) */
  consolidationInterval?: number
  /** OpenAI API key for embeddings */
  openAIApiKey?: string
}

// ============================================================================
// Memory Manager
// ============================================================================

/**
 * Unified memory manager coordinating all memory subsystems
 */
export class MemoryManager implements IMemoryManager {
  readonly episodic: EpisodicMemoryManager
  readonly semantic: SemanticMemoryManager
  readonly procedural: ProceduralMemoryManager

  private store: VectorStore
  private embedder: Embedder
  private consolidation: ConsolidationEngine
  private consolidationTimer: ReturnType<typeof setInterval> | null = null
  private listeners = new Set<MemoryEventListener>()

  constructor(options: MemoryManagerOptions = {}) {
    // Initialize store
    this.store = options.store ?? new SQLiteVectorStore()

    // Initialize embedder with caching
    const baseEmbedder = options.embedder ?? createOpenAIEmbedder({ apiKey: options.openAIApiKey })
    this.embedder = new CachingEmbedder(baseEmbedder)

    // Initialize subsystems
    this.episodic = new EpisodicMemoryManager(this.store, this.embedder)
    this.semantic = new SemanticMemoryManager(this.store, this.embedder)
    this.procedural = new ProceduralMemoryManager(this.store, this.embedder)

    // Initialize consolidation
    this.consolidation = new ConsolidationEngine(this.store, {
      decayRate: options.decayRate ?? DEFAULT_DECAY_RATE,
    })

    // Set up auto-consolidation
    if (options.consolidationInterval && options.consolidationInterval > 0) {
      this.consolidationTimer = setInterval(() => {
        void this.consolidate()
      }, options.consolidationInterval)
    }
  }

  // ==========================================================================
  // IMemoryManager Implementation
  // ==========================================================================

  /**
   * Remember something (auto-routes to appropriate subsystem)
   */
  async remember(
    entry: CreateEpisodicMemoryInput | CreateSemanticMemoryInput | CreateProceduralMemoryInput,
    type: MemoryType
  ): Promise<MemoryId> {
    let id: MemoryId

    switch (type) {
      case 'episodic':
        id = await this.episodic.recordSession(entry as CreateEpisodicMemoryInput)
        break
      case 'semantic':
        id = await this.semantic.learn(entry as CreateSemanticMemoryInput)
        break
      case 'procedural':
        id = await this.procedural.recordPattern(entry as CreateProceduralMemoryInput)
        break
      default:
        throw new Error(`Unknown memory type: ${type}`)
    }

    this.emit({ type: 'memory_created', id, memoryType: type })
    return id
  }

  /**
   * Recall memories matching a query
   */
  async recall(query: MemoryQuery): Promise<MemoryEntry[]> {
    return this.store.query(query)
  }

  /**
   * Recall similar memories by text
   */
  async recallSimilar(text: string, limit = 5, type?: MemoryType): Promise<SimilarityResult[]> {
    const embedding = await this.embedder.embed(text)
    return this.store.findSimilar(embedding, limit, type)
  }

  /**
   * Reinforce a memory
   */
  async reinforce(id: MemoryId): Promise<void> {
    const entry = await this.store.get(id)
    if (!entry) return

    // Route to appropriate manager
    switch (entry.type) {
      case 'episodic':
        await this.episodic.recordAccess(id)
        break
      case 'semantic':
        await this.semantic.reinforce(id)
        break
      case 'procedural':
        await this.procedural.recordOutcome(id, true)
        break
    }

    const updated = await this.store.get(id)
    if (updated) {
      this.emit({ type: 'memory_reinforced', id, newImportance: updated.metadata.importance })
    }
  }

  /**
   * Forget a memory
   */
  async forget(id: MemoryId): Promise<void> {
    await this.store.delete(id)
    this.emit({ type: 'memory_deleted', id })
  }

  /**
   * Run memory consolidation
   */
  async consolidate(): Promise<ConsolidationResult> {
    const result = await this.consolidation.consolidate()
    this.emit({ type: 'consolidation_complete', result })
    return result
  }

  // ==========================================================================
  // Additional Methods
  // ==========================================================================

  /**
   * Get a memory by ID
   */
  async get(id: MemoryId): Promise<MemoryEntry | null> {
    return this.store.get(id)
  }

  /**
   * Count memories
   */
  async count(type?: MemoryType): Promise<number> {
    return this.store.count(type)
  }

  /**
   * Get statistics for all memory types
   */
  async getStats(): Promise<{
    total: number
    episodic: number
    semantic: number
    procedural: number
  }> {
    const [total, episodic, semantic, procedural] = await Promise.all([
      this.store.count(),
      this.store.count('episodic'),
      this.store.count('semantic'),
      this.store.count('procedural'),
    ])

    return { total, episodic, semantic, procedural }
  }

  /**
   * Initialize the memory store
   */
  async initialize(): Promise<void> {
    if (this.store instanceof SQLiteVectorStore) {
      await this.store.initialize()
    }
  }

  // ==========================================================================
  // Events
  // ==========================================================================

  /**
   * Subscribe to memory events
   */
  on(listener: MemoryEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /**
   * Emit event to listeners
   */
  private emit(event: MemoryEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch (err) {
        console.warn('Memory event listener error:', err)
      }
    }
  }

  // ==========================================================================
  // Cleanup
  // ==========================================================================

  /**
   * Dispose of the memory manager
   */
  dispose(): void {
    if (this.consolidationTimer) {
      clearInterval(this.consolidationTimer)
      this.consolidationTimer = null
    }

    this.listeners.clear()
  }
}

// ============================================================================
// Singleton
// ============================================================================

let _instance: MemoryManager | null = null

/**
 * Get the global memory manager instance
 */
export function getMemoryManager(): MemoryManager {
  if (!_instance) {
    _instance = new MemoryManager()
  }
  return _instance
}

/**
 * Set the global memory manager instance (for testing)
 */
export function setMemoryManager(manager: MemoryManager | null): void {
  _instance = manager
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a memory manager instance
 */
export function createMemoryManager(options?: MemoryManagerOptions): MemoryManager {
  return new MemoryManager(options)
}
