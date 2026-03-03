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

export interface EnhancedStuckConfig {
  repeatedCallThreshold: number
  errorCycleThreshold: number
  emptyTurnThreshold: number
  monologueThreshold: number
  tokenWasteRatio: number
  tokenWasteMinTurns: number
  selfAssessmentEveryTurns: number
}

const DEFAULT_CONFIG: DoomLoopConfig = {
  threshold: 3,
  historySize: 10,
}

const DEFAULT_ENHANCED_CONFIG: EnhancedStuckConfig = {
  repeatedCallThreshold: 3,
  errorCycleThreshold: 3,
  emptyTurnThreshold: 3,
  monologueThreshold: 5,
  tokenWasteRatio: 0.5,
  tokenWasteMinTurns: 3,
  selfAssessmentEveryTurns: 10,
}

let config = { ...DEFAULT_CONFIG }
let enhancedConfig = { ...DEFAULT_ENHANCED_CONFIG }
const sessions = new Map<string, RecordedToolCall[]>()
const enhancedSessions = new Map<string, EnhancedSessionState>()

export function configure(partial: Partial<DoomLoopConfig>): void {
  config = { ...config, ...partial }
}

export function getConfig(): DoomLoopConfig {
  return { ...config }
}

export function resetDoomLoop(): void {
  sessions.clear()
  config = { ...DEFAULT_CONFIG }
  enhancedSessions.clear()
  enhancedConfig = { ...DEFAULT_ENHANCED_CONFIG }
}

export function configureEnhanced(partial: Partial<EnhancedStuckConfig>): void {
  enhancedConfig = { ...enhancedConfig, ...partial }
}

interface EnhancedToolCall {
  name: string
  args: Record<string, unknown>
  success: boolean
  result?: string
}

interface EnhancedSessionState {
  turnCount: number
  recentSignatures: string[]
  lastError: string | null
  consecutiveErrorCount: number
  noToolTurns: number
  wastedTokens: number
  totalTokens: number
  noProgressTurns: number
  lastScenarioTurn: Map<string, number>
}

function stableArgsHash(args: Record<string, unknown>): string {
  const keys = Object.keys(args).sort()
  return keys.map((k) => `${k}:${JSON.stringify(args[k])}`).join('|')
}

function toSignature(call: EnhancedToolCall): string {
  return `${call.name}:${stableArgsHash(call.args)}`
}

function getState(sessionId: string): EnhancedSessionState {
  const current = enhancedSessions.get(sessionId)
  if (current) return current
  const created: EnhancedSessionState = {
    turnCount: 0,
    recentSignatures: [],
    lastError: null,
    consecutiveErrorCount: 0,
    noToolTurns: 0,
    wastedTokens: 0,
    totalTokens: 0,
    noProgressTurns: 0,
    lastScenarioTurn: new Map(),
  }
  enhancedSessions.set(sessionId, created)
  return created
}

function canEmitScenario(state: EnhancedSessionState, scenario: string): boolean {
  const lastTurn = state.lastScenarioTurn.get(scenario)
  if (lastTurn === undefined) {
    state.lastScenarioTurn.set(scenario, state.turnCount)
    return true
  }
  if (state.turnCount - lastTurn >= 2) {
    state.lastScenarioTurn.set(scenario, state.turnCount)
    return true
  }
  return false
}

function evaluateRepeatedCalls(state: EnhancedSessionState): { detected: boolean; count: number } {
  const last = state.recentSignatures.at(-1)
  if (!last) return { detected: false, count: 0 }
  let count = 0
  for (let i = state.recentSignatures.length - 1; i >= 0; i--) {
    if (state.recentSignatures[i] === last) count++
    else break
  }
  return {
    detected: count >= enhancedConfig.repeatedCallThreshold,
    count,
  }
}

function evaluateErrorCycling(state: EnhancedSessionState): { detected: boolean; count: number } {
  return {
    detected: state.consecutiveErrorCount >= enhancedConfig.errorCycleThreshold,
    count: state.consecutiveErrorCount,
  }
}

function detectLikelyError(call: EnhancedToolCall): string | null {
  if (!call.success) return (call.result ?? 'error').slice(0, 240)
  const text = (call.result ?? '').toLowerCase()
  if (text.includes('error') || text.includes('failed')) return text.slice(0, 240)
  return null
}

function evaluateSelfAssessment(state: EnhancedSessionState): boolean {
  if (state.turnCount === 0 || state.turnCount % enhancedConfig.selfAssessmentEveryTurns !== 0) {
    return false
  }
  return state.noProgressTurns >= 2 || state.noToolTurns >= 2
}

function emitStuck(
  api: ExtensionAPI,
  sessionId: string,
  scenario: string,
  count: number,
  suggestion: string,
  severity: 'low' | 'medium' | 'high'
): void {
  api.emit('stuck:detected', {
    sessionId,
    scenario,
    count,
    severity,
    suggestion,
  })
  api.log.warn(`[stuck:${scenario}] ${suggestion}`)
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

// ─── Global (Registry-Level) Doom Loop Detection ─────────────────────────────

const globalToolCallLog: Array<{ agentId: string; hash: string; timestamp: number }> = []
const MAX_GLOBAL_LOG = 100

export function trackGlobalToolCall(
  agentId: string,
  toolName: string,
  args: Record<string, unknown>
): void {
  const hash = `${toolName}:${JSON.stringify(args)}`
  globalToolCallLog.push({ agentId, hash, timestamp: Date.now() })
  if (globalToolCallLog.length > MAX_GLOBAL_LOG) globalToolCallLog.shift()
}

export function detectGlobalDoomLoop(windowMs: number = 60_000): {
  detected: boolean
  pattern?: string
  count?: number
} {
  const cutoff = Date.now() - windowMs
  const recent = globalToolCallLog.filter((e) => e.timestamp > cutoff)

  // Count frequency of each hash across all agents
  const counts = new Map<string, number>()
  for (const entry of recent) {
    counts.set(entry.hash, (counts.get(entry.hash) ?? 0) + 1)
  }

  for (const [hash, count] of counts) {
    if (count >= 5) {
      return { detected: true, pattern: hash, count }
    }
  }

  return { detected: false }
}

export function resetGlobalDoomLoop(): void {
  globalToolCallLog.length = 0
}

export function getGlobalToolCallLog(): ReadonlyArray<{
  agentId: string
  hash: string
  timestamp: number
}> {
  return globalToolCallLog
}

export function registerDoomLoop(api: ExtensionAPI): Disposable {
  const toolDisposable = api.on('tool:finish', (data: unknown) => {
    const event = data as {
      name?: string
      toolName?: string
      args?: Record<string, unknown>
      sessionId?: string
      agentId?: string
    }
    const tool = event.name ?? event.toolName
    const sessionId = event.sessionId ?? event.agentId
    if (!tool || !sessionId) return

    const result = check(sessionId, tool, event.args ?? {})
    if (!result.detected) return

    api.emit('doom-loop:detected', {
      sessionId,
      tool,
      consecutiveCount: result.consecutiveCount,
      suggestion: result.suggestion,
    })
    api.log.warn(`Doom loop detected: ${result.suggestion}`)
  })

  const turnDisposable = api.on('turn:end', (data: unknown) => {
    const event = data as {
      agentId?: string
      turn?: number
      toolCalls?: Array<{
        name: string
        args?: Record<string, unknown>
        success: boolean
        result?: string
      }>
    }
    const sessionId = event.agentId
    if (!sessionId) return

    const state = getState(sessionId)
    state.turnCount = event.turn ?? state.turnCount + 1

    const toolCalls: EnhancedToolCall[] = (event.toolCalls ?? []).map((call) => ({
      name: call.name,
      args: call.args ?? {},
      success: call.success,
      result: call.result,
    }))

    if (toolCalls.length === 0) {
      state.noToolTurns += 1
      state.noProgressTurns += 1
    } else {
      state.noToolTurns = 0
      state.noProgressTurns = 0
    }

    for (const call of toolCalls) {
      state.recentSignatures.push(toSignature(call))
      if (state.recentSignatures.length > 25) state.recentSignatures.shift()

      const error = detectLikelyError(call)
      if (error) {
        if (state.lastError === error) state.consecutiveErrorCount += 1
        else {
          state.lastError = error
          state.consecutiveErrorCount = 1
        }
      } else {
        state.lastError = null
        state.consecutiveErrorCount = 0
      }
    }

    const repeated = evaluateRepeatedCalls(state)
    if (repeated.detected && canEmitScenario(state, 'repeated-tool-call')) {
      emitStuck(
        api,
        sessionId,
        'repeated-tool-call',
        repeated.count,
        `Detected ${repeated.count} identical consecutive tool calls`,
        'high'
      )
      const tool = toolCalls.at(-1)?.name
      if (tool) {
        api.emit('doom-loop:detected', {
          sessionId,
          tool,
          consecutiveCount: repeated.count,
          suggestion: `Tool "${tool}" has been called ${repeated.count} times with the same arguments. Try a different approach.`,
        })
      }
    }

    const cycling = evaluateErrorCycling(state)
    if (cycling.detected && canEmitScenario(state, 'error-cycling')) {
      emitStuck(
        api,
        sessionId,
        'error-cycling',
        cycling.count,
        'Repeated identical error pattern detected; switch strategy or inspect root cause.',
        'high'
      )
    }

    if (
      state.noToolTurns >= enhancedConfig.emptyTurnThreshold &&
      canEmitScenario(state, 'empty-response-loop')
    ) {
      emitStuck(
        api,
        sessionId,
        'empty-response-loop',
        state.noToolTurns,
        'Several turns completed without tool activity.',
        'medium'
      )
    }

    if (
      state.noToolTurns >= enhancedConfig.monologueThreshold &&
      canEmitScenario(state, 'monologue-loop')
    ) {
      emitStuck(
        api,
        sessionId,
        'monologue-loop',
        state.noToolTurns,
        'Agent appears to be monologuing without taking actions.',
        'medium'
      )
    }

    if (evaluateSelfAssessment(state) && canEmitScenario(state, 'self-assessment')) {
      emitStuck(
        api,
        sessionId,
        'self-assessment',
        state.turnCount,
        'Self-assessment indicates low progress. Consider decomposing task or resetting context.',
        'low'
      )
    }
  })

  const usageDisposable = api.on('llm:usage', (data: unknown) => {
    const event = data as { sessionId?: string; inputTokens?: number; outputTokens?: number }
    if (!event.sessionId) return
    const state = getState(event.sessionId)
    const spent = (event.inputTokens ?? 0) + (event.outputTokens ?? 0)
    state.totalTokens += spent
    if (state.noProgressTurns > 0) {
      state.wastedTokens += spent
    }

    const ratio = state.totalTokens === 0 ? 0 : state.wastedTokens / state.totalTokens
    if (
      state.noProgressTurns >= enhancedConfig.tokenWasteMinTurns &&
      ratio > enhancedConfig.tokenWasteRatio &&
      canEmitScenario(state, 'token-waste')
    ) {
      emitStuck(
        api,
        event.sessionId,
        'token-waste',
        state.noProgressTurns,
        `High token spend without progress (${Math.round(ratio * 100)}% wasted).`,
        'high'
      )
    }
  })

  return {
    dispose() {
      toolDisposable.dispose()
      turnDisposable.dispose()
      usageDisposable.dispose()
    },
  }
}
