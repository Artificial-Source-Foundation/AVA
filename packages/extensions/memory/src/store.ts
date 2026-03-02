/**
 * Memory store — CRUD over ExtensionStorage.
 *
 * Categories: project, preferences, debug, context.
 * Each memory entry has a key, value, category, and timestamp.
 */

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

  async read(key: string): Promise<MemoryEntry | null> {
    await this.ensureLoaded()
    return this.entries.get(key) ?? null
  }

  async list(category?: MemoryCategory): Promise<MemoryEntry[]> {
    await this.ensureLoaded()
    const all = [...this.entries.values()]
    if (category) return all.filter((e) => e.category === category)
    return all
  }

  async remove(key: string): Promise<boolean> {
    await this.ensureLoaded()
    const had = this.entries.delete(key)
    if (had) await this.persist()
    return had
  }

  async buildPromptSection(): Promise<string> {
    await this.ensureLoaded()
    if (this.entries.size === 0) return ''

    const lines = ['<memories>']
    const byCategory = new Map<MemoryCategory, MemoryEntry[]>()
    for (const entry of this.entries.values()) {
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
}
