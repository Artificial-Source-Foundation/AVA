import type { InstructionFile } from './types.js'

interface CacheEntry {
  readonly files: InstructionFile[]
  readonly at: number
}

export class DiscoveryCache {
  private readonly entries = new Map<string, CacheEntry>()
  private hits = 0
  private misses = 0

  constructor(
    private readonly ttlMs = 5 * 60_000,
    private readonly now: () => number = Date.now
  ) {}

  get(key: string): InstructionFile[] | null {
    const entry = this.entries.get(key)
    if (!entry) {
      this.misses++
      return null
    }
    if (this.now() - entry.at > this.ttlMs) {
      this.entries.delete(key)
      this.misses++
      return null
    }
    this.hits++
    return entry.files
  }

  set(key: string, files: InstructionFile[]): void {
    this.entries.set(key, { files, at: this.now() })
  }

  clear(): void {
    this.entries.clear()
  }

  stats(): { hits: number; misses: number; entries: number } {
    return {
      hits: this.hits,
      misses: this.misses,
      entries: this.entries.size,
    }
  }
}
