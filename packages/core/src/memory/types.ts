/**
 * Memory Types
 *
 * Type definitions for the long-term memory system.
 * Supports episodic (session), semantic (facts), and procedural (patterns) memory.
 */

// ============================================================================
// Core Types
// ============================================================================

/** Memory type categories */
export type MemoryType = 'episodic' | 'semantic' | 'procedural'

/** Memory entry identifier */
export type MemoryId = string

// ============================================================================
// Memory Entry
// ============================================================================

/** Metadata for a memory entry */
export interface MemoryMetadata {
  /** Creation timestamp */
  timestamp: number
  /** Importance score (0-1) */
  importance: number
  /** Number of times this memory has been accessed */
  accessCount: number
  /** Last access timestamp */
  lastAccessed: number
  /** Tags for categorization */
  tags: string[]
  /** Source of the memory (e.g., session ID, user input) */
  source?: string
  /** Confidence score for semantic memories (0-1) */
  confidence?: number
  /** Success rate for procedural memories (0-1) */
  successRate?: number
}

/** Base memory entry */
export interface MemoryEntry {
  /** Unique identifier */
  id: MemoryId
  /** Memory type */
  type: MemoryType
  /** Memory content (text) */
  content: string
  /** Vector embedding (1536 dimensions for text-embedding-3-small) */
  embedding?: Float32Array
  /** Memory metadata */
  metadata: MemoryMetadata
}

// ============================================================================
// Episodic Memory
// ============================================================================

/** Session summary for episodic memory */
export interface EpisodicMemory extends MemoryEntry {
  type: 'episodic'
  /** Session ID this memory is from */
  sessionId: string
  /** Summary of what was done */
  summary: string
  /** Key decisions made during the session */
  decisions: string[]
  /** Tools used during the session */
  toolsUsed: string[]
  /** Outcome of the session */
  outcome: 'success' | 'partial' | 'failure' | 'abandoned'
  /** Duration in minutes */
  durationMinutes: number
}

/** Create episodic memory input */
export interface CreateEpisodicMemoryInput {
  sessionId: string
  summary: string
  decisions: string[]
  toolsUsed: string[]
  outcome: EpisodicMemory['outcome']
  durationMinutes: number
  tags?: string[]
}

// ============================================================================
// Semantic Memory
// ============================================================================

/** Learned fact for semantic memory */
export interface SemanticMemory extends MemoryEntry {
  type: 'semantic'
  /** The fact or concept */
  fact: string
  /** Where this fact was learned from */
  source: string
  /** How certain we are about this fact (0-1) */
  confidence: number
  /** Related memory IDs */
  relatedIds?: MemoryId[]
}

/** Create semantic memory input */
export interface CreateSemanticMemoryInput {
  fact: string
  source: string
  confidence?: number
  tags?: string[]
  relatedIds?: MemoryId[]
}

// ============================================================================
// Procedural Memory
// ============================================================================

/** Learned pattern for procedural memory */
export interface ProceduralMemory extends MemoryEntry {
  type: 'procedural'
  /** Context that triggers this pattern */
  context: string
  /** The action or approach to take */
  action: string
  /** Tools involved in this pattern */
  tools: string[]
  /** Number of times this pattern was used */
  useCount: number
  /** Number of successful uses */
  successCount: number
  /** Calculated success rate (successCount / useCount) */
  successRate: number
}

/** Create procedural memory input */
export interface CreateProceduralMemoryInput {
  context: string
  action: string
  tools: string[]
  success: boolean
  tags?: string[]
}

// ============================================================================
// Query Types
// ============================================================================

/** Memory query options */
export interface MemoryQuery {
  /** Filter by memory type */
  type?: MemoryType | MemoryType[]
  /** Filter by tags (any match) */
  tags?: string[]
  /** Minimum importance score */
  minImportance?: number
  /** Maximum age in milliseconds */
  maxAge?: number
  /** Maximum results to return */
  limit?: number
  /** Skip first N results */
  offset?: number
  /** Sort order */
  orderBy?: 'timestamp' | 'importance' | 'lastAccessed' | 'accessCount'
  /** Sort direction */
  order?: 'asc' | 'desc'
}

/** Similarity search result */
export interface SimilarityResult {
  memory: MemoryEntry
  similarity: number
}

// ============================================================================
// Store Types
// ============================================================================

/** Vector store interface for memory persistence */
export interface VectorStore {
  /** Insert a memory entry */
  insert(entry: MemoryEntry): Promise<void>
  /** Get a memory by ID */
  get(id: MemoryId): Promise<MemoryEntry | null>
  /** Update a memory entry */
  update(id: MemoryId, updates: Partial<MemoryEntry>): Promise<void>
  /** Delete a memory entry */
  delete(id: MemoryId): Promise<void>
  /** Query memories */
  query(query: MemoryQuery): Promise<MemoryEntry[]>
  /** Find similar memories by embedding */
  findSimilar(
    embedding: Float32Array,
    limit: number,
    type?: MemoryType
  ): Promise<SimilarityResult[]>
  /** Count memories by type */
  count(type?: MemoryType): Promise<number>
  /** Get all memory IDs */
  getAllIds(type?: MemoryType): Promise<MemoryId[]>
}

// ============================================================================
// Manager Types
// ============================================================================

/** Memory manager interface */
export interface IMemoryManager {
  /** Remember something (auto-determines type based on input) */
  remember(
    entry: CreateEpisodicMemoryInput | CreateSemanticMemoryInput | CreateProceduralMemoryInput,
    type: MemoryType
  ): Promise<MemoryId>
  /** Recall memories matching a query */
  recall(query: MemoryQuery): Promise<MemoryEntry[]>
  /** Recall similar memories by text */
  recallSimilar(text: string, limit?: number, type?: MemoryType): Promise<SimilarityResult[]>
  /** Reinforce a memory (boost importance) */
  reinforce(id: MemoryId): Promise<void>
  /** Forget a memory */
  forget(id: MemoryId): Promise<void>
  /** Run memory consolidation (decay, merge, promote) */
  consolidate(): Promise<ConsolidationResult>
}

/** Result of consolidation operation */
export interface ConsolidationResult {
  /** Number of memories that decayed below threshold */
  decayed: number
  /** Number of similar memories that were merged */
  merged: number
  /** Number of memories that were promoted */
  promoted: number
  /** Total memories after consolidation */
  totalRemaining: number
}

// ============================================================================
// Event Types
// ============================================================================

/** Memory events */
export type MemoryEvent =
  | { type: 'memory_created'; id: MemoryId; memoryType: MemoryType }
  | { type: 'memory_updated'; id: MemoryId }
  | { type: 'memory_deleted'; id: MemoryId }
  | { type: 'memory_reinforced'; id: MemoryId; newImportance: number }
  | { type: 'consolidation_complete'; result: ConsolidationResult }

/** Memory event listener */
export type MemoryEventListener = (event: MemoryEvent) => void

// ============================================================================
// Embedding Types
// ============================================================================

/** Embedder interface */
export interface Embedder {
  /** Generate embedding for text */
  embed(text: string): Promise<Float32Array>
  /** Generate embeddings for multiple texts (batch) */
  embedBatch(texts: string[]): Promise<Float32Array[]>
  /** Get embedding dimensions */
  getDimensions(): number
}

// ============================================================================
// Constants
// ============================================================================

/** Embedding dimensions for text-embedding-3-small */
export const EMBEDDING_DIMENSIONS = 1536

/** Default importance for new memories */
export const DEFAULT_IMPORTANCE = 0.5

/** Minimum importance before decay removal */
export const MIN_IMPORTANCE_THRESHOLD = 0.1

/** Similarity threshold for duplicate detection */
export const DUPLICATE_SIMILARITY_THRESHOLD = 0.95

/** Success rate threshold for procedural memory suggestions */
export const SUCCESS_RATE_THRESHOLD = 0.7
