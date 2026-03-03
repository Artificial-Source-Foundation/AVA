/**
 * Token tracker — tracks token usage per session.
 */

export interface TokenStats {
  inputTokens: number
  outputTokens: number
  totalTokens: number
  turnCount: number
  cacheReadTokens: number
  cacheCreationTokens: number
}

const sessions = new Map<string, TokenStats>()

export function trackTokens(
  sessionId: string,
  input: number,
  output: number,
  cacheRead?: number,
  cacheCreation?: number
): void {
  const existing = sessions.get(sessionId) ?? {
    inputTokens: 0,
    outputTokens: 0,
    totalTokens: 0,
    turnCount: 0,
    cacheReadTokens: 0,
    cacheCreationTokens: 0,
  }

  existing.inputTokens += input
  existing.outputTokens += output
  existing.totalTokens += input + output
  existing.cacheReadTokens += cacheRead ?? 0
  existing.cacheCreationTokens += cacheCreation ?? 0
  existing.turnCount++

  sessions.set(sessionId, existing)
}

export function getTokenStats(sessionId: string): TokenStats | null {
  return sessions.get(sessionId) ?? null
}

export function resetTokenStats(sessionId?: string): void {
  if (sessionId) {
    sessions.delete(sessionId)
  } else {
    sessions.clear()
  }
}
