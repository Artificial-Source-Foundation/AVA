import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

export interface WindowState {
  path: string
  firstLine: number
  windowSize: number
  totalLines: number
}

export class WindowedFileView {
  private lines: string[] = []
  private state: WindowState | null = null
  private readonly cache = new Map<string, string[]>()

  constructor(private windowSize: number = 100) {}

  setFileContent(path: string, content: string): void {
    this.cache.set(path, content.split('\n'))
  }

  open(path: string, startLine = 1): WindowState {
    const lines = this.cache.get(path)
    if (!lines) {
      throw new Error(`Windowed view content not loaded for ${path}`)
    }

    this.lines = lines
    const totalLines = this.lines.length
    const firstLine = this.clamp(startLine - 1, totalLines)
    this.state = { path, firstLine, windowSize: this.windowSize, totalLines }
    return this.state
  }

  goto(line: number): WindowState {
    const state = this.requireState()
    const centered = Math.max(0, line - 1 - Math.floor(state.windowSize / 2))
    state.firstLine = this.clamp(centered, state.totalLines)
    return state
  }

  scrollUp(lines = Math.floor(this.windowSize / 2)): WindowState {
    const state = this.requireState()
    state.firstLine = this.clamp(state.firstLine - lines, state.totalLines)
    return state
  }

  scrollDown(lines = Math.floor(this.windowSize / 2)): WindowState {
    const state = this.requireState()
    state.firstLine = this.clamp(state.firstLine + lines, state.totalLines)
    return state
  }

  getWindowText(state: WindowState): string {
    if (state.totalLines <= 200) {
      return this.lines.map((line, idx) => `${idx + 1}: ${line}`).join('\n')
    }

    const end = Math.min(state.totalLines, state.firstLine + state.windowSize)
    const chunk = this.lines.slice(state.firstLine, end)
    return chunk.map((line, idx) => `${state.firstLine + idx + 1}: ${line}`).join('\n')
  }

  formatStatus(state: WindowState): string {
    const end = Math.min(state.totalLines, state.firstLine + state.windowSize)
    const above = state.firstLine
    const below = Math.max(0, state.totalLines - end)
    return `[File: ${state.path} (${state.totalLines} lines)] (${above} more above, ${below} more below)`
  }

  private requireState(): WindowState {
    if (!this.state) {
      throw new Error('No file opened in windowed view')
    }
    return this.state
  }

  private clamp(firstLine: number, totalLines: number): number {
    const maxStart = Math.max(0, totalLines - this.windowSize)
    return Math.max(0, Math.min(firstLine, maxStart))
  }
}

interface SessionWindow {
  view: WindowedFileView
  state: WindowState
}

const sessions = new Map<string, SessionWindow>()

function requireSession(sessionId: string): SessionWindow {
  const session = sessions.get(sessionId)
  if (!session) {
    throw new Error('No file opened for windowed mode. Use view_window with a path first.')
  }
  return session
}

const ViewWindowSchema = z.object({
  path: z.string().optional(),
  startLine: z.number().int().min(1).optional(),
})

const ScrollSchema = z.object({
  lines: z.number().int().min(1).optional(),
})

const GotoSchema = z.object({
  line: z.number().int().min(1),
})

export const viewWindowTool = defineTool({
  name: 'view_window',
  description: 'Show current file window with status line.',
  schema: ViewWindowSchema,
  permissions: ['read'],
  async execute(input, ctx) {
    let session = sessions.get(ctx.sessionId)

    if (input.path) {
      const view = session?.view ?? new WindowedFileView(100)
      const content = await getPlatform().fs.readFile(input.path)
      view.setFileContent(input.path, content)
      const state = view.open(input.path, input.startLine)
      session = { view, state }
      sessions.set(ctx.sessionId, session)
    }

    if (!session) {
      throw new Error('No file opened for windowed mode. Pass a path to view_window first.')
    }

    return {
      success: true,
      output: `${session.view.formatStatus(session.state)}\n${session.view.getWindowText(session.state)}`,
      metadata: { window: session.state },
    }
  },
})

export const scrollUpTool = defineTool({
  name: 'scroll_up',
  description: 'Move the current window upward.',
  schema: ScrollSchema,
  permissions: ['read'],
  async execute(input, ctx) {
    const session = requireSession(ctx.sessionId)
    session.state = session.view.scrollUp(input.lines)
    return {
      success: true,
      output: `${session.view.formatStatus(session.state)}\n${session.view.getWindowText(session.state)}`,
      metadata: { window: session.state },
    }
  },
})

export const scrollDownTool = defineTool({
  name: 'scroll_down',
  description: 'Move the current window downward.',
  schema: ScrollSchema,
  permissions: ['read'],
  async execute(input, ctx) {
    const session = requireSession(ctx.sessionId)
    session.state = session.view.scrollDown(input.lines)
    return {
      success: true,
      output: `${session.view.formatStatus(session.state)}\n${session.view.getWindowText(session.state)}`,
      metadata: { window: session.state },
    }
  },
})

export const gotoLineTool = defineTool({
  name: 'goto_line',
  description: 'Center the file window around a target line.',
  schema: GotoSchema,
  permissions: ['read'],
  async execute(input, ctx) {
    const session = requireSession(ctx.sessionId)
    session.state = session.view.goto(input.line)
    return {
      success: true,
      output: `${session.view.formatStatus(session.state)}\n${session.view.getWindowText(session.state)}`,
      metadata: { window: session.state },
    }
  },
})

export function resetWindowedSessions(): void {
  sessions.clear()
}
