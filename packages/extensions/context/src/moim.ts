import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { getTokenStats } from './tracker.js'

const DEFAULT_CONTEXT_LIMIT = 200_000
const DEFAULT_MAX_TURNS = 50

export function activateMoim(api: ExtensionAPI): Disposable {
  let sessionId = ''
  let cwd = ''
  let currentTurn = 0
  let activeExtensions = 0

  const d1 = api.on('session:opened', (data) => {
    const payload = data as { sessionId?: string; workingDirectory?: string }
    sessionId = payload.sessionId ?? sessionId
    cwd = payload.workingDirectory ?? cwd
    currentTurn = 0
  })

  const d2 = api.on('turn:start', (data) => {
    const payload = data as { turn?: number }
    currentTurn = payload.turn ?? currentTurn
  })

  const d3 = api.on('extensions:loaded', (data) => {
    const payload = data as { count?: number }
    activeExtensions = payload.count ?? activeExtensions
  })

  const d4 = api.on('prompt:build', (data) => {
    const payload = data as { sections?: string[] }
    if (!payload.sections) return

    const stats = sessionId ? getTokenStats(sessionId) : null
    const tokenPct = Math.min(
      100,
      Math.round((((stats?.totalTokens ?? 0) * 100) / DEFAULT_CONTEXT_LIMIT) * 10) / 10
    )
    const turnDisplay = currentTurn > 0 ? currentTurn : 1

    payload.sections.push(
      `[Context] CWD: ${cwd || 'unknown'} | Tokens: ${tokenPct}% of 200K | Turn: ${turnDisplay}/${DEFAULT_MAX_TURNS} | Extensions: ${activeExtensions} active`
    )
  })

  return {
    dispose() {
      d1.dispose()
      d2.dispose()
      d3.dispose()
      d4.dispose()
    },
  }
}
