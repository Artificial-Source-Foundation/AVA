/**
 * Memory store — CRUD over ExtensionStorage.
 *
 * Categories: project, preferences, debug, context.
 * Each memory entry has a key, value, category, and timestamp.
 */

import { dispatchCompute } from '@ava/core-v2'
import type { ExtensionStorage } from '@ava/core-v2/extensions'

export type MemoryCategory = 'project' | 'preferences' | 'debug' | 'context'

export interface MemoryEntry {
  key: string
  value: string
  category: MemoryCategory
  createdAt: number
  updatedAt: number
}

const STORAGE_KEY = 'memories'
const DEFAULT_DB_PATH = 'ava.db'

interface RustMemoryEntry {
  id: number
  key: string
  value: string
  createdAt: string
}

function isRustMemoryEntry(value: MemoryEntry | RustMemoryEntry): value is RustMemoryEntry {
  return 'id' in value && typeof value.createdAt === 'string'
}

export class MemoryStore {
  private entries = new Map<string, MemoryEntry>()
  private loaded = false

  constructor(private storage: ExtensionStorage) {}

  private async ensureLoaded(): Promise<void> {
    if (this.loaded) return
    const saved = await this.storage.get<Record<string, MemoryEntry>>(STORAGE_KEY)
    if (saved) {
      for (const [key, entry] of Object.entries(saved)) {
        this.entries.set(key, entry)
      }
    }
    this.loaded = true
  }

  private async persist(): Promise<void> {
    await this.storage.set(STORAGE_KEY, Object.fromEntries(this.entries))
  }

  /** Public flush — persists all in-memory entries to storage. */
  async flush(): Promise<void> {
    await this.ensureLoaded()
    await this.persist()
  }

  async write(
    key: string,
    value: string,
    category: MemoryCategory = 'project'
  ): Promise<MemoryEntry> {
    const result = await dispatchCompute<RustMemoryEntry | MemoryEntry>(
      'memory_remember',
      { dbPath: DEFAULT_DB_PATH, key, value },
      async () => this.writeLocal(key, value, category)
    )

    if (!isRustMemoryEntry(result)) {
      return result
    }

    await this.ensureLoaded()
    const existing = this.entries.get(result.key)
    const now = Date.now()
    const entry: MemoryEntry = {
      key: result.key,
      value: result.value,
      category: existing?.category ?? category,
      createdAt: existing?.createdAt ?? (Date.parse(result.createdAt) || now),
      updatedAt: now,
    }
    this.entries.set(entry.key, entry)
    await this.persist()
    return entry
  }

  async read(key: string): Promise<MemoryEntry | null> {
    const result = await dispatchCompute<RustMemoryEntry | MemoryEntry | null>(
      'memory_recall',
      { dbPath: DEFAULT_DB_PATH, key },
      async () => this.readLocal(key)
    )

    if (!result || !isRustMemoryEntry(result)) {
      return result
    }

    await this.ensureLoaded()
    const existing = this.entries.get(result.key)
    const now = Date.now()
    const entry: MemoryEntry = {
      key: result.key,
      value: result.value,
      category: existing?.category ?? 'project',
      createdAt: existing?.createdAt ?? (Date.parse(result.createdAt) || now),
      updatedAt: now,
    }
    this.entries.set(entry.key, entry)
    await this.persist()
    return entry
  }

  async list(category?: MemoryCategory): Promise<MemoryEntry[]> {
    const result = await dispatchCompute<RustMemoryEntry[] | MemoryEntry[]>(
      'memory_recent',
      { dbPath: DEFAULT_DB_PATH, limit: 500 },
      async () => this.listLocal()
    )

    const firstEntry = result[0]
    if (!firstEntry || !isRustMemoryEntry(firstEntry)) {
      const entries = result as MemoryEntry[]
      if (category) {
        return entries.filter((entry) => entry.category === category)
      }
      return entries
    }

    await this.ensureLoaded()
    const now = Date.now()
    const rustEntries = result as RustMemoryEntry[]
    const mapped = rustEntries.map((entry) => {
      const existing = this.entries.get(entry.key)
      return {
        key: entry.key,
        value: entry.value,
        category: existing?.category ?? 'project',
        createdAt: existing?.createdAt ?? (Date.parse(entry.createdAt) || now),
        updatedAt: now,
      } satisfies MemoryEntry
    })

    for (const entry of mapped) {
      this.entries.set(entry.key, entry)
    }
    await this.persist()
    if (category) {
      return mapped.filter((entry) => entry.category === category)
    }

    return mapped
  }

  async search(query: string, category?: MemoryCategory): Promise<MemoryEntry[]> {
    const result = await dispatchCompute<RustMemoryEntry[] | MemoryEntry[]>(
      'memory_search',
      { dbPath: DEFAULT_DB_PATH, query },
      async () => {
        const entries = await this.listLocal(category)
        const normalized = query.toLowerCase()
        return entries.filter(
          (entry) =>
            entry.key.toLowerCase().includes(normalized) ||
            entry.value.toLowerCase().includes(normalized)
        )
      }
    )

    const firstEntry = result[0]
    if (!firstEntry || !isRustMemoryEntry(firstEntry)) {
      const entries = result as MemoryEntry[]
      if (category) {
        return entries.filter((entry) => entry.category === category)
      }
      return entries
    }

    await this.ensureLoaded()
    const now = Date.now()
    const rustEntries = result as RustMemoryEntry[]
    const mapped = rustEntries.map((entry) => {
      const existing = this.entries.get(entry.key)
      return {
        key: entry.key,
        value: entry.value,
        category: existing?.category ?? 'project',
        createdAt: existing?.createdAt ?? (Date.parse(entry.createdAt) || now),
        updatedAt: now,
      } satisfies MemoryEntry
    })

    for (const entry of mapped) {
      this.entries.set(entry.key, entry)
    }
    await this.persist()

    if (category) {
      return mapped.filter((entry) => entry.category === category)
    }

    return mapped
  }

  async remove(key: string): Promise<boolean> {
    // TODO(sprint-2): Rust memory command surface has no delete API yet; keep TS path.
    await this.ensureLoaded()
    const had = this.entries.delete(key)
    if (had) await this.persist()
    return had
  }

  async buildPromptSection(): Promise<string> {
    const entries = await this.list()
    if (entries.length === 0) return ''

    const lines = ['<memories>']
    const byCategory = new Map<MemoryCategory, MemoryEntry[]>()
    for (const entry of entries) {
      const list = byCategory.get(entry.category) ?? []
      list.push(entry)
      byCategory.set(entry.category, list)
    }

    for (const [cat, entries] of byCategory) {
      lines.push(`  [${cat}]`)
      for (const e of entries) {
        lines.push(`  - ${e.key}: ${e.value}`)
      }
    }
    lines.push('</memories>')
    return lines.join('\n')
  }

  private async writeLocal(
    key: string,
    value: string,
    category: MemoryCategory = 'project'
  ): Promise<MemoryEntry> {
    await this.ensureLoaded()
    const existing = this.entries.get(key)
    const now = Date.now()
    const entry: MemoryEntry = {
      key,
      value,
      category,
      createdAt: existing?.createdAt ?? now,
      updatedAt: now,
    }
    this.entries.set(key, entry)
    await this.persist()
    return entry
  }

  private async readLocal(key: string): Promise<MemoryEntry | null> {
    await this.ensureLoaded()
    return this.entries.get(key) ?? null
  }

  private async listLocal(category?: MemoryCategory): Promise<MemoryEntry[]> {
    await this.ensureLoaded()
    const all = [...this.entries.values()]
    if (category) return all.filter((e) => e.category === category)
    return all
  }
}
