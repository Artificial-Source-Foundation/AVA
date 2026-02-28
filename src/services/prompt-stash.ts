/**
 * Prompt Stash Service
 * localStorage-based stash for saving and restoring prompt drafts.
 */

const STASH_KEY = 'ava-prompt-stash'
const MAX_STASH = 20

export function getStash(): string[] {
  try {
    const raw = localStorage.getItem(STASH_KEY)
    if (raw) return JSON.parse(raw) as string[]
  } catch {
    /* ignore */
  }
  return []
}

export function pushStash(text: string): void {
  const stash = getStash()
  stash.unshift(text)
  if (stash.length > MAX_STASH) stash.length = MAX_STASH
  localStorage.setItem(STASH_KEY, JSON.stringify(stash))
}

export function popStash(): string | undefined {
  const stash = getStash()
  if (stash.length === 0) return undefined
  const text = stash.shift()
  localStorage.setItem(STASH_KEY, JSON.stringify(stash))
  return text
}

export function peekStash(): string | undefined {
  const stash = getStash()
  return stash[0]
}

export function clearStash(): void {
  localStorage.removeItem(STASH_KEY)
}

export function stashCount(): number {
  return getStash().length
}
