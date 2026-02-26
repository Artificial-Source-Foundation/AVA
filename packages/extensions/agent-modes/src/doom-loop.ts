/**
 * Doom loop detector — detects when the agent makes identical tool calls repeatedly.
 *
 * Subscribes to tool:finish events and tracks call patterns per session.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export interface DoomLoopConfig {
  threshold: number
  historySize: number
}

export interface RecordedToolCall {
  tool: string
  paramsHash: string
  timestamp: number
}

export interface DoomLoopCheckResult {
  detected: boolean
  consecutiveCount: number
  repeatedCall?: { tool: string }
  suggestion?: string
}

const DEFAULT_CONFIG: DoomLoopConfig = {
  threshold: 3,
  historySize: 10,
}

let config = { ...DEFAULT_CONFIG }
const sessions = new Map<string, RecordedToolCall[]>()

export function configure(partial: Partial<DoomLoopConfig>): void {
  config = { ...config, ...partial }
}

export function getConfig(): DoomLoopConfig {
  return { ...config }
}

export function resetDoomLoop(): void {
  sessions.clear()
  config = { ...DEFAULT_CONFIG }
}

function hashParams(params: Record<string, unknown>): string {
  const sorted = Object.keys(params)
    .sort()
    .map((k) => `${k}:${JSON.stringify(params[k])}`)
    .join('|')
  // Simple string hash
  let hash = 0
  for (let i = 0; i < sorted.length; i++) {
    const char = sorted.charCodeAt(i)
    hash = ((hash << 5) - hash + char) | 0
  }
  return hash.toString(36)
}

export function check(
  sessionId: string,
  tool: string,
  params: Record<string, unknown>
): DoomLoopCheckResult {
  const paramsHash = hashParams(params)
  const record: RecordedToolCall = { tool, paramsHash, timestamp: Date.now() }

  let history = sessions.get(sessionId)
  if (!history) {
    history = []
    sessions.set(sessionId, history)
  }

  history.push(record)

  // Trim to historySize
  if (history.length > config.historySize) {
    history.splice(0, history.length - config.historySize)
  }

  // Count consecutive identical calls from the end
  let count = 0
  for (let i = history.length - 1; i >= 0; i--) {
    const entry = history[i]!
    if (entry.tool === tool && entry.paramsHash === paramsHash) {
      count++
    } else {
      break
    }
  }

  const detected = count >= config.threshold
  return {
    detected,
    consecutiveCount: count,
    repeatedCall: detected ? { tool } : undefined,
    suggestion: detected
      ? `Tool "${tool}" has been called ${count} times with the same arguments. Try a different approach.`
      : undefined,
  }
}

export function clearSession(sessionId: string): void {
  sessions.delete(sessionId)
}

export function getHistory(sessionId: string): readonly RecordedToolCall[] {
  return sessions.get(sessionId) ?? []
}

export function registerDoomLoop(api: ExtensionAPI): Disposable {
  const disposable = api.on('tool:finish', (data: unknown) => {
    const event = data as { name: string; args?: Record<string, unknown>; sessionId?: string }
    if (event.name && event.sessionId) {
      const result = check(event.sessionId, event.name, event.args ?? {})
      if (result.detected) {
        api.emit('doom-loop:detected', {
          sessionId: event.sessionId,
          tool: event.name,
          consecutiveCount: result.consecutiveCount,
          suggestion: result.suggestion,
        })
        api.log.warn(`Doom loop detected: ${result.suggestion}`)
      }
    }
  })

  return disposable
}
