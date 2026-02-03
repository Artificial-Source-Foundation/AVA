/**
 * Memory Vector Store
 *
 * SQLite-based storage for memory entries with vector embeddings.
 * Embeddings are stored as BLOBs (Float32Array buffers).
 * Similarity search is performed in JavaScript using cosine similarity.
 */

import type { IDatabase, Migration } from '../platform.js'
import { getPlatform } from '../platform.js'
import type {
  MemoryEntry,
  MemoryId,
  MemoryMetadata,
  MemoryQuery,
  MemoryType,
  SimilarityResult,
  VectorStore,
} from './types.js'

// ============================================================================
// Database Types
// ============================================================================

/** Row from memories table */
interface MemoryRow {
  id: string
  type: string
  content: string
  embedding: Uint8Array | null
  metadata: string
  created_at: number
}

// ============================================================================
// Migrations
// ============================================================================

const MIGRATIONS: Migration[] = [
  {
    version: 1,
    name: 'create_memories_table',
    up: `
      CREATE TABLE IF NOT EXISTS memories (
        id TEXT PRIMARY KEY,
        type TEXT NOT NULL,
        content TEXT NOT NULL,
        embedding BLOB,
        metadata TEXT NOT NULL,
        created_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_memories_type ON memories(type);
      CREATE INDEX IF NOT EXISTS idx_memories_created_at ON memories(created_at);
    `,
    down: 'DROP TABLE IF EXISTS memories;',
  },
]

// ============================================================================
// SQLite Vector Store
// ============================================================================

/**
 * SQLite-based vector store for memory persistence
 */
export class SQLiteVectorStore implements VectorStore {
  private db: IDatabase
  private initialized = false

  constructor(database?: IDatabase) {
    this.db = database ?? getPlatform().database
  }

  /**
   * Initialize the store (run migrations)
   */
  async initialize(): Promise<void> {
    if (this.initialized) return
    await this.db.migrate(MIGRATIONS)
    this.initialized = true
  }

  /**
   * Insert a memory entry
   */
  async insert(entry: MemoryEntry): Promise<void> {
    await this.ensureInitialized()

    const embeddingBlob = entry.embedding ? this.float32ToBlob(entry.embedding) : null

    await this.db.execute(
      `INSERT INTO memories (id, type, content, embedding, metadata, created_at)
       VALUES (?, ?, ?, ?, ?, ?)`,
      [
        entry.id,
        entry.type,
        entry.content,
        embeddingBlob,
        JSON.stringify(entry.metadata),
        entry.metadata.timestamp,
      ]
    )
  }

  /**
   * Get a memory by ID
   */
  async get(id: MemoryId): Promise<MemoryEntry | null> {
    await this.ensureInitialized()

    const rows = await this.db.query<MemoryRow>('SELECT * FROM memories WHERE id = ?', [id])

    if (rows.length === 0) return null
    return this.rowToEntry(rows[0])
  }

  /**
   * Update a memory entry
   */
  async update(id: MemoryId, updates: Partial<MemoryEntry>): Promise<void> {
    await this.ensureInitialized()

    const existing = await this.get(id)
    if (!existing) {
      throw new Error(`Memory not found: ${id}`)
    }

    const merged: MemoryEntry = {
      ...existing,
      ...updates,
      metadata: {
        ...existing.metadata,
        ...updates.metadata,
      },
    }

    const embeddingBlob = merged.embedding ? this.float32ToBlob(merged.embedding) : null

    await this.db.execute(
      `UPDATE memories SET type = ?, content = ?, embedding = ?, metadata = ? WHERE id = ?`,
      [merged.type, merged.content, embeddingBlob, JSON.stringify(merged.metadata), id]
    )
  }

  /**
   * Delete a memory entry
   */
  async delete(id: MemoryId): Promise<void> {
    await this.ensureInitialized()
    await this.db.execute('DELETE FROM memories WHERE id = ?', [id])
  }

  /**
   * Query memories
   */
  async query(query: MemoryQuery): Promise<MemoryEntry[]> {
    await this.ensureInitialized()

    const conditions: string[] = []
    const params: unknown[] = []

    // Type filter
    if (query.type) {
      if (Array.isArray(query.type)) {
        conditions.push(`type IN (${query.type.map(() => '?').join(', ')})`)
        params.push(...query.type)
      } else {
        conditions.push('type = ?')
        params.push(query.type)
      }
    }

    // Max age filter
    if (query.maxAge !== undefined) {
      const cutoff = Date.now() - query.maxAge
      conditions.push('created_at >= ?')
      params.push(cutoff)
    }

    // Build query
    let sql = 'SELECT * FROM memories'
    if (conditions.length > 0) {
      sql += ` WHERE ${conditions.join(' AND ')}`
    }

    // Ordering
    const orderBy = query.orderBy ?? 'timestamp'
    const order = query.order ?? 'desc'
    const orderColumn = orderBy === 'timestamp' ? 'created_at' : orderBy
    sql += ` ORDER BY json_extract(metadata, '$.${orderColumn}') ${order.toUpperCase()}`

    // Pagination
    const limit = query.limit ?? 100
    sql += ` LIMIT ${limit}`
    if (query.offset) {
      sql += ` OFFSET ${query.offset}`
    }

    const rows = await this.db.query<MemoryRow>(sql, params)
    const entries = rows.map((row) => this.rowToEntry(row))

    // Post-filter by tags and minImportance (JSON operations in SQLite are limited)
    return entries.filter((entry) => {
      // Min importance filter
      if (query.minImportance !== undefined && entry.metadata.importance < query.minImportance) {
        return false
      }

      // Tags filter (any match)
      if (query.tags && query.tags.length > 0) {
        const hasMatchingTag = query.tags.some((tag) => entry.metadata.tags.includes(tag))
        if (!hasMatchingTag) return false
      }

      return true
    })
  }

  /**
   * Find similar memories by embedding using cosine similarity
   */
  async findSimilar(
    embedding: Float32Array,
    limit: number,
    type?: MemoryType
  ): Promise<SimilarityResult[]> {
    await this.ensureInitialized()

    // Fetch all memories with embeddings
    let sql = 'SELECT * FROM memories WHERE embedding IS NOT NULL'
    const params: unknown[] = []

    if (type) {
      sql += ' AND type = ?'
      params.push(type)
    }

    const rows = await this.db.query<MemoryRow>(sql, params)

    // Calculate similarities in JavaScript
    const results: SimilarityResult[] = []

    for (const row of rows) {
      if (!row.embedding) continue

      const entryEmbedding = this.blobToFloat32(row.embedding)
      const similarity = this.cosineSimilarity(embedding, entryEmbedding)

      results.push({
        memory: this.rowToEntry(row),
        similarity,
      })
    }

    // Sort by similarity (descending) and take top N
    return results.sort((a, b) => b.similarity - a.similarity).slice(0, limit)
  }

  /**
   * Count memories by type
   */
  async count(type?: MemoryType): Promise<number> {
    await this.ensureInitialized()

    let sql = 'SELECT COUNT(*) as count FROM memories'
    const params: unknown[] = []

    if (type) {
      sql += ' WHERE type = ?'
      params.push(type)
    }

    const rows = await this.db.query<{ count: number }>(sql, params)
    return rows[0]?.count ?? 0
  }

  /**
   * Get all memory IDs
   */
  async getAllIds(type?: MemoryType): Promise<MemoryId[]> {
    await this.ensureInitialized()

    let sql = 'SELECT id FROM memories'
    const params: unknown[] = []

    if (type) {
      sql += ' WHERE type = ?'
      params.push(type)
    }

    const rows = await this.db.query<{ id: string }>(sql, params)
    return rows.map((row) => row.id)
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  private async ensureInitialized(): Promise<void> {
    if (!this.initialized) {
      await this.initialize()
    }
  }

  private rowToEntry(row: MemoryRow): MemoryEntry {
    return {
      id: row.id,
      type: row.type as MemoryType,
      content: row.content,
      embedding: row.embedding ? this.blobToFloat32(row.embedding) : undefined,
      metadata: JSON.parse(row.metadata) as MemoryMetadata,
    }
  }

  /**
   * Convert Float32Array to BLOB (Uint8Array)
   */
  private float32ToBlob(arr: Float32Array): Uint8Array {
    return new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength)
  }

  /**
   * Convert BLOB (Uint8Array) to Float32Array
   */
  private blobToFloat32(blob: Uint8Array): Float32Array {
    // Create a copy to ensure proper alignment
    const buffer = new ArrayBuffer(blob.length)
    const view = new Uint8Array(buffer)
    view.set(blob)
    return new Float32Array(buffer)
  }

  /**
   * Calculate cosine similarity between two vectors
   */
  private cosineSimilarity(a: Float32Array, b: Float32Array): number {
    if (a.length !== b.length) {
      throw new Error(`Vector length mismatch: ${a.length} vs ${b.length}`)
    }

    let dotProduct = 0
    let normA = 0
    let normB = 0

    for (let i = 0; i < a.length; i++) {
      dotProduct += a[i] * b[i]
      normA += a[i] * a[i]
      normB += b[i] * b[i]
    }

    normA = Math.sqrt(normA)
    normB = Math.sqrt(normB)

    if (normA === 0 || normB === 0) {
      return 0
    }

    return dotProduct / (normA * normB)
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a SQLite vector store instance
 */
export function createVectorStore(database?: IDatabase): SQLiteVectorStore {
  return new SQLiteVectorStore(database)
}
