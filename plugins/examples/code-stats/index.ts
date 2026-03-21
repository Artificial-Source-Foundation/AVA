import * as fs from 'node:fs'
import * as path from 'node:path'
import { createPlugin } from '@ava-ai/plugin'

/** Tracks in-flight tool calls by call_id → start time (ms). */
const pendingCalls: Record<string, number> = {}

/** Accumulated durations per tool name: toolName → ms[] */
const toolDurations: Record<string, number[]> = {}

/** Total calls started this session. */
let totalCalls = 0

/** Ensure the .ava directory exists and return the log file path. */
function logPath(projectDir: string): string {
  const avaDir = path.join(projectDir || '.', '.ava')
  if (!fs.existsSync(avaDir)) {
    fs.mkdirSync(avaDir, { recursive: true })
  }
  return path.join(avaDir, 'code-stats.log')
}

/** Append a line to the log file, creating it if needed. */
function appendLog(logFile: string, line: string): void {
  fs.appendFileSync(logFile, `${line}\n`, 'utf-8')
}

createPlugin({
  'tool.before': async (ctx, params) => {
    const toolName = (params.tool as string | undefined) ?? 'unknown'
    const callId = (params.call_id as string | undefined) ?? String(Date.now())
    const now = new Date()

    totalCalls++
    pendingCalls[callId] = now.getTime()

    const line = `[BEFORE] ${now.toISOString()} tool=${toolName} call_id=${callId}`
    appendLog(logPath(ctx.project.directory), line)
    process.stderr.write(`[code-stats] -> ${toolName} (${callId})\n`)

    // Pass args through unchanged
    return { args: params.args ?? {} }
  },

  'tool.after': async (ctx, params) => {
    const toolName = (params.tool as string | undefined) ?? 'unknown'
    const callId = (params.call_id as string | undefined) ?? '?'
    const error = params.error as string | undefined
    const status = error ? 'error' : 'ok'

    const startMs = pendingCalls[callId]
    const durationMs = startMs !== undefined ? Date.now() - startMs : 0
    delete pendingCalls[callId]

    // Accumulate for summary
    if (toolDurations[toolName] === undefined) {
      toolDurations[toolName] = []
    }
    toolDurations[toolName].push(durationMs)

    const line = `[AFTER]  ${new Date().toISOString()} tool=${toolName} call_id=${callId} status=${status} duration_ms=${durationMs}`
    appendLog(logPath(ctx.project.directory), line)
    process.stderr.write(`[code-stats] <- ${toolName} (${callId}) ${durationMs}ms [${status}]\n`)

    return {}
  },

  'session.end': async (ctx, params) => {
    const sessionId = (params.session_id as string | undefined) ?? 'unknown'
    const entries = Object.entries(toolDurations)

    const logFile = logPath(ctx.project.directory)

    const summaryLines: string[] = [
      '',
      `[SUMMARY] session_id=${sessionId} total_calls=${totalCalls}`,
    ]

    if (entries.length === 0) {
      summaryLines.push('[SUMMARY] No tools were called this session.')
    } else {
      for (const [name, durations] of entries) {
        const count = durations.length
        const total = durations.reduce((a, b) => a + b, 0)
        const avg = count > 0 ? (total / count).toFixed(0) : '0'
        const min = Math.min(...durations)
        const max = Math.max(...durations)
        summaryLines.push(
          `[SUMMARY] ${name}: calls=${count} avg=${avg}ms min=${min}ms max=${max}ms total=${total}ms`
        )
      }
    }
    summaryLines.push('')

    // Write summary to log file
    for (const line of summaryLines) {
      appendLog(logFile, line)
    }

    // Also print summary to stderr for visibility
    process.stderr.write('\n[code-stats] === Session Summary ===\n')
    process.stderr.write(`[code-stats] Session: ${sessionId}\n`)
    process.stderr.write(`[code-stats] Total calls: ${totalCalls}\n`)
    for (const [name, durations] of entries) {
      const count = durations.length
      const avg = count > 0 ? (durations.reduce((a, b) => a + b, 0) / count).toFixed(0) : '0'
      process.stderr.write(`[code-stats] ${name}: ${count}x avg ${avg}ms\n`)
    }
    process.stderr.write('[code-stats] ===========================\n\n')

    return {}
  },
})
