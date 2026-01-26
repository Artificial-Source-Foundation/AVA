/**
 * Delta9 Dashboard Command
 *
 * Real-time TUI dashboard showing:
 * - Agent status grid (active/idle/completed)
 * - Message queue visualization
 * - Budget burn rate
 * - Recent decision traces
 * - Active subagents tree
 * - Mission progress
 */

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { DashboardOptions } from '../types.js'
import { colorize, symbols } from '../types.js'

// =============================================================================
// Types
// =============================================================================

interface AgentStatus {
  name: string
  state: 'active' | 'idle' | 'completed' | 'failed' | 'unknown'
  lastActivity?: string
  currentTask?: string
}

interface DashboardData {
  mission: MissionSnapshot | null
  agents: AgentStatus[]
  subagents: SubagentSnapshot[]
  messages: MessageSnapshot
  budget: BudgetSnapshot
  traces: TraceSnapshot[]
  sessions: SessionSnapshot[]
}

interface MissionSnapshot {
  id: string
  description: string
  status: string
  progress: number
  totalTasks: number
  completedTasks: number
  failedTasks: number
}

interface SubagentSnapshot {
  alias: string
  state: string
  agentType: string
  spawnedAt: string
}

interface MessageSnapshot {
  total: number
  unread: number
  byAgent: Record<string, number>
}

interface BudgetSnapshot {
  spent: number
  limit: number
  percentage: number
  burnRate: number // per hour
  timeRemaining?: number // hours
}

interface TraceSnapshot {
  id: string
  type: string
  decision: string
  confidence: number
  timestamp: string
}

interface SessionSnapshot {
  sessionId: string
  agentId: string
  state: string
  pendingMessages: number
}

// =============================================================================
// ASCII Art Helpers
// =============================================================================

function drawBox(title: string, content: string[], width: number = 40): string[] {
  const lines: string[] = []
  const innerWidth = width - 4 // Account for borders and padding

  // Top border
  lines.push(`┌─ ${title} ${'─'.repeat(Math.max(0, innerWidth - title.length - 1))}┐`)

  // Content
  for (const line of content) {
    const truncated = line.slice(0, innerWidth)
    const padding = ' '.repeat(Math.max(0, innerWidth - truncated.length))
    lines.push(`│ ${truncated}${padding} │`)
  }

  // Bottom border
  lines.push(`└${'─'.repeat(width - 2)}┘`)

  return lines
}

function progressBar(percent: number, width: number = 20): string {
  const filled = Math.round((percent / 100) * width)
  const empty = width - filled
  const bar = '█'.repeat(filled) + '░'.repeat(empty)
  return `[${bar}] ${percent.toFixed(0)}%`
}

function stateEmoji(state: string): string {
  switch (state) {
    case 'active':
      return '🟢'
    case 'idle':
      return '🟡'
    case 'completed':
      return '✅'
    case 'failed':
      return '❌'
    case 'spawning':
      return '⏳'
    default:
      return '⬜'
  }
}

function sparkline(values: number[], width: number = 10): string {
  if (values.length === 0) return '▁'.repeat(width)

  const max = Math.max(...values, 1)
  const chars = ['▁', '▂', '▃', '▄', '▅', '▆', '▇', '█']

  const normalized = values.slice(-width)
  return normalized.map((v) => chars[Math.floor((v / max) * 7)]).join('')
}

// =============================================================================
// Data Loading
// =============================================================================

function loadDashboardData(cwd: string): DashboardData {
  const data: DashboardData = {
    mission: null,
    agents: [],
    subagents: [],
    messages: { total: 0, unread: 0, byAgent: {} },
    budget: { spent: 0, limit: 0, percentage: 0, burnRate: 0 },
    traces: [],
    sessions: [],
  }

  // Load mission
  const missionFile = join(cwd, '.delta9', 'mission.json')
  if (existsSync(missionFile)) {
    try {
      const mission = JSON.parse(readFileSync(missionFile, 'utf-8'))
      const tasks = mission.objectives?.flatMap((o: { tasks: unknown[] }) => o.tasks || []) || []
      const completed = tasks.filter((t: { status: string }) => t.status === 'completed').length
      const failed = tasks.filter((t: { status: string }) => t.status === 'failed').length

      data.mission = {
        id: mission.id || 'unknown',
        description: mission.description || 'No description',
        status: mission.status || 'unknown',
        progress: tasks.length > 0 ? Math.round((completed / tasks.length) * 100) : 0,
        totalTasks: tasks.length,
        completedTasks: completed,
        failedTasks: failed,
      }
    } catch {
      // Ignore parse errors
    }
  }

  // Load traces
  const tracesFile = join(cwd, '.delta9', 'traces.jsonl')
  if (existsSync(tracesFile)) {
    try {
      const lines = readFileSync(tracesFile, 'utf-8').trim().split('\n').filter(Boolean)
      data.traces = lines
        .slice(-5)
        .reverse()
        .map((line) => {
          const trace = JSON.parse(line)
          return {
            id: trace.id,
            type: trace.type,
            decision: trace.decision?.slice(0, 50) || '',
            confidence: trace.confidence || 0,
            timestamp: trace.timestamp || '',
          }
        })
    } catch {
      // Ignore parse errors
    }
  }

  // Load messages
  const messagesFile = join(cwd, '.delta9', 'messages.jsonl')
  if (existsSync(messagesFile)) {
    try {
      const lines = readFileSync(messagesFile, 'utf-8').trim().split('\n').filter(Boolean)
      data.messages.total = lines.length

      for (const line of lines) {
        try {
          const msg = JSON.parse(line)
          if (!msg.readAt) data.messages.unread++
          const to = msg.to || 'unknown'
          data.messages.byAgent[to] = (data.messages.byAgent[to] || 0) + 1
        } catch {
          // Skip invalid lines
        }
      }
    } catch {
      // Ignore parse errors
    }
  }

  // Load budget from events
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')
  if (existsSync(eventsFile)) {
    try {
      const lines = readFileSync(eventsFile, 'utf-8').trim().split('\n').filter(Boolean)
      let firstTimestamp: number | null = null
      let lastTimestamp: number | null = null

      for (const line of lines) {
        try {
          const event = JSON.parse(line)
          const ts = new Date(event.timestamp).getTime()

          if (!firstTimestamp) firstTimestamp = ts
          lastTimestamp = ts

          // Extract budget from relevant events
          if (event.data?.budgetSpent) {
            data.budget.spent += event.data.budgetSpent
          }
        } catch {
          // Skip invalid lines
        }
      }

      // Calculate burn rate
      if (firstTimestamp && lastTimestamp && lastTimestamp > firstTimestamp) {
        const hours = (lastTimestamp - firstTimestamp) / (1000 * 60 * 60)
        if (hours > 0) {
          data.budget.burnRate = data.budget.spent / hours
        }
      }
    } catch {
      // Ignore parse errors
    }
  }

  // Calculate budget percentage
  if (data.budget.limit > 0) {
    data.budget.percentage = (data.budget.spent / data.budget.limit) * 100
    if (data.budget.burnRate > 0) {
      data.budget.timeRemaining = (data.budget.limit - data.budget.spent) / data.budget.burnRate
    }
  }

  // Mock agent status (would come from actual session tracking in production)
  data.agents = [
    { name: 'commander', state: data.mission ? 'active' : 'idle' },
    { name: 'operator', state: data.mission?.status === 'in_progress' ? 'active' : 'idle' },
    { name: 'validator', state: 'idle' },
    { name: 'oracle_claude', state: 'idle' },
    { name: 'oracle_gpt', state: 'idle' },
    { name: 'scout', state: 'idle' },
    { name: 'intel', state: 'idle' },
  ]

  return data
}

// =============================================================================
// Rendering
// =============================================================================

function renderMissionPanel(data: DashboardData): string[] {
  const content: string[] = []

  if (data.mission) {
    content.push(`ID: ${data.mission.id}`)
    content.push(
      `Status: ${colorize(data.mission.status, data.mission.status === 'completed' ? 'green' : 'yellow')}`
    )
    content.push(`Description:`)
    content.push(`  ${data.mission.description.slice(0, 34)}`)
    content.push('')
    content.push(`Progress: ${progressBar(data.mission.progress, 20)}`)
    content.push(
      `Tasks: ${data.mission.completedTasks}/${data.mission.totalTasks} ${symbols.checkmark}  ${data.mission.failedTasks} ${symbols.cross}`
    )
  } else {
    content.push(colorize('No active mission', 'dim'))
    content.push('')
    content.push('Use mission_create to start')
  }

  return drawBox('Mission', content, 42)
}

function renderAgentsPanel(data: DashboardData): string[] {
  const content: string[] = []

  // Grid layout: 2 columns
  for (let i = 0; i < data.agents.length; i += 2) {
    const left = data.agents[i]
    const right = data.agents[i + 1]

    let line = `${stateEmoji(left.state)} ${left.name.padEnd(12)}`
    if (right) {
      line += ` ${stateEmoji(right.state)} ${right.name}`
    }
    content.push(line)
  }

  content.push('')
  content.push('🟢 active  🟡 idle  ✅ done')

  return drawBox('Agents', content, 42)
}

function renderSubagentsPanel(data: DashboardData): string[] {
  const content: string[] = []

  if (data.subagents.length === 0) {
    content.push(colorize('No active subagents', 'dim'))
    content.push('')
    content.push('Use spawn_subagent to create')
  } else {
    for (const sub of data.subagents.slice(0, 4)) {
      content.push(`${stateEmoji(sub.state)} ${sub.alias}`)
      content.push(`    ${sub.agentType} @ ${sub.spawnedAt.slice(11, 19)}`)
    }
    if (data.subagents.length > 4) {
      content.push(`... and ${data.subagents.length - 4} more`)
    }
  }

  return drawBox('Subagents', content, 42)
}

function renderMessagesPanel(data: DashboardData): string[] {
  const content: string[] = []

  content.push(
    `Total: ${data.messages.total}  Unread: ${colorize(String(data.messages.unread), data.messages.unread > 0 ? 'yellow' : 'dim')}`
  )
  content.push('')

  const sorted = Object.entries(data.messages.byAgent)
    .sort((a, b) => b[1] - a[1])
    .slice(0, 3)

  if (sorted.length > 0) {
    content.push('Top recipients:')
    for (const [agent, count] of sorted) {
      content.push(`  ${agent}: ${count}`)
    }
  } else {
    content.push(colorize('No messages', 'dim'))
  }

  return drawBox('Messages', content, 42)
}

function renderBudgetPanel(data: DashboardData): string[] {
  const content: string[] = []

  const spentStr = `$${data.budget.spent.toFixed(2)}`
  const limitStr = data.budget.limit > 0 ? `$${data.budget.limit.toFixed(2)}` : 'unlimited'

  content.push(`Spent: ${spentStr} / ${limitStr}`)

  if (data.budget.limit > 0) {
    content.push(progressBar(Math.min(data.budget.percentage, 100), 20))
  }

  content.push('')

  if (data.budget.burnRate > 0) {
    content.push(`Burn rate: $${data.budget.burnRate.toFixed(2)}/hr`)
    // Show sparkline for burn rate trend (mock data for now)
    const trend = [1, 2, 3, 2, 4, 3, 5, 4, 6, data.budget.burnRate]
    content.push(`Trend: ${sparkline(trend, 10)}`)
    if (data.budget.timeRemaining) {
      content.push(`Time remaining: ${data.budget.timeRemaining.toFixed(1)}h`)
    }
  } else {
    content.push(colorize('No burn rate data', 'dim'))
  }

  return drawBox('Budget', content, 42)
}

function renderTracesPanel(data: DashboardData): string[] {
  const content: string[] = []

  if (data.traces.length === 0) {
    content.push(colorize('No recent traces', 'dim'))
    content.push('')
    content.push('Use trace_decision to record')
  } else {
    for (const trace of data.traces.slice(0, 3)) {
      const conf = `${(trace.confidence * 100).toFixed(0)}%`
      content.push(`[${trace.type.slice(0, 10)}] ${conf}`)
      content.push(`  ${trace.decision.slice(0, 34)}`)
    }
  }

  return drawBox('Recent Traces', content, 42)
}

function renderSessionsPanel(data: DashboardData): string[] {
  const content: string[] = []

  const active = data.sessions.filter((s) => s.state === 'active').length
  const idle = data.sessions.filter((s) => s.state === 'idle').length
  const pending = data.sessions.filter((s) => s.pendingMessages > 0).length

  content.push(`Active: ${active}  Idle: ${idle}`)
  content.push(`With pending: ${colorize(String(pending), pending > 0 ? 'yellow' : 'dim')}`)
  content.push('')

  for (const session of data.sessions.slice(0, 2)) {
    content.push(`${stateEmoji(session.state)} ${session.agentId}`)
    if (session.pendingMessages > 0) {
      content.push(`  ${session.pendingMessages} pending`)
    }
  }

  if (data.sessions.length === 0) {
    content.push(colorize('No sessions registered', 'dim'))
  }

  return drawBox('Sessions', content, 42)
}

// =============================================================================
// Dashboard Command
// =============================================================================

export async function dashboardCommand(options: DashboardOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()

  // Load data
  const data = loadDashboardData(cwd)

  // Clear screen
  console.log('\x1b[2J\x1b[H')

  // Header
  console.log(colorize('═'.repeat(86), 'blue'))
  console.log(
    colorize('  Delta9 Dashboard', 'blue') + '  ' + colorize(new Date().toLocaleString(), 'dim')
  )
  console.log(colorize('═'.repeat(86), 'blue'))
  console.log('')

  // Two-column layout
  const leftPanels = [renderMissionPanel(data), renderAgentsPanel(data), renderSubagentsPanel(data)]

  const rightPanels = [
    renderBudgetPanel(data),
    renderMessagesPanel(data),
    renderTracesPanel(data),
    renderSessionsPanel(data),
  ]

  // Render side by side
  const maxLines = Math.max(
    leftPanels.reduce((sum, p) => sum + p.length, 0),
    rightPanels.reduce((sum, p) => sum + p.length, 0)
  )

  let leftIndex = 0
  let rightIndex = 0
  let leftPanelIndex = 0
  let rightPanelIndex = 0

  for (let i = 0; i < maxLines; i++) {
    let leftLine = ''
    let rightLine = ''

    // Get left line
    if (leftPanelIndex < leftPanels.length) {
      const panel = leftPanels[leftPanelIndex]
      if (leftIndex < panel.length) {
        leftLine = panel[leftIndex]
        leftIndex++
      } else {
        leftPanelIndex++
        leftIndex = 0
        if (leftPanelIndex < leftPanels.length) {
          leftLine = leftPanels[leftPanelIndex][leftIndex] || ''
          leftIndex++
        }
      }
    }

    // Get right line
    if (rightPanelIndex < rightPanels.length) {
      const panel = rightPanels[rightPanelIndex]
      if (rightIndex < panel.length) {
        rightLine = panel[rightIndex]
        rightIndex++
      } else {
        rightPanelIndex++
        rightIndex = 0
        if (rightPanelIndex < rightPanels.length) {
          rightLine = rightPanels[rightPanelIndex][rightIndex] || ''
          rightIndex++
        }
      }
    }

    // Pad left line and print both
    const paddedLeft = leftLine.padEnd(44)
    console.log(paddedLeft + rightLine)
  }

  console.log('')
  console.log(colorize('═'.repeat(86), 'blue'))
  console.log(colorize('  Press Ctrl+C to exit', 'dim'))

  // If watch mode, refresh
  if (options.watch) {
    const interval = options.interval || 2000
    await new Promise((resolve) => setTimeout(resolve, interval))
    await dashboardCommand(options)
  }
}
