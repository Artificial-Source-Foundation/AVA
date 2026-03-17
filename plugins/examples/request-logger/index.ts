import * as fs from 'node:fs'
import * as path from 'node:path'
import { createPlugin } from '@ava-ai/plugin'

function getLogPath(projectDir: string): string {
  const dir = path.join(projectDir || '.', '.ava')
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true })
  }
  return path.join(dir, 'tool-log.jsonl')
}

function appendLog(logPath: string, entry: Record<string, unknown>): void {
  const line = JSON.stringify(entry) + '\n'
  fs.appendFileSync(logPath, line, 'utf-8')
}

createPlugin({
  'tool.before': async (ctx, params) => {
    const logPath = getLogPath(ctx.project.directory)
    appendLog(logPath, {
      timestamp: new Date().toISOString(),
      phase: 'before',
      tool: params.tool,
      args: params.args,
    })
    return { args: params.args }
  },

  'tool.after': async (ctx, params) => {
    const logPath = getLogPath(ctx.project.directory)
    appendLog(logPath, {
      timestamp: new Date().toISOString(),
      phase: 'after',
      tool: params.tool,
      result: params.result,
      error: params.error,
    })
    return {}
  },
})
