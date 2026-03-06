/**
 * Layout Persistence Helpers
 * Safe localStorage read/write utilities for layout state.
 */

export function loadString<T extends string>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key)
    if (raw) return raw as T
  } catch {
    /* ignore */
  }
  return fallback
}

export function loadBool(key: string, fallback: boolean): boolean {
  try {
    const raw = localStorage.getItem(key)
    if (raw !== null) return raw === 'true'
  } catch {
    /* ignore */
  }
  return fallback
}

export function loadNumber(key: string, fallback: number, min: number, max: number): number {
  try {
    const raw = localStorage.getItem(key)
    if (raw) {
      const n = Number(raw)
      if (n >= min && n <= max) return n
    }
  } catch {
    /* ignore */
  }
  return fallback
}

export function save(key: string, value: string): void {
  try {
    localStorage.setItem(key, value)
  } catch {
    /* ignore */
  }
}
