/**
 * Workflow Scheduler
 *
 * Cron-based scheduling for automated workflow execution.
 * Parses cron expressions, computes next run times, and runs
 * a polling loop that triggers workflows on schedule.
 */

// ============================================================================
// Types
// ============================================================================

export interface CronSchedule {
  minute: CronField
  hour: CronField
  dayOfMonth: CronField
  month: CronField
  dayOfWeek: CronField
}

export interface CronField {
  type: 'wildcard' | 'value' | 'list' | 'range' | 'step'
  values: number[]
}

export interface ScheduledWorkflow {
  id: string
  cron: string
  lastRun?: number
}

// ============================================================================
// Parser
// ============================================================================

function parseField(field: string, min: number, max: number): CronField {
  // Wildcard
  if (field === '*') {
    return { type: 'wildcard', values: [] }
  }

  // Step: */5 or 1-10/2
  if (field.includes('/')) {
    const [base, stepStr] = field.split('/')
    const step = parseInt(stepStr, 10)
    const values: number[] = []
    let start = min
    let end = max
    if (base !== '*') {
      if (base.includes('-')) {
        const [lo, hi] = base.split('-').map(Number)
        start = lo
        end = hi
      } else {
        start = parseInt(base, 10)
      }
    }
    for (let i = start; i <= end; i += step) {
      values.push(i)
    }
    return { type: 'step', values }
  }

  // List: 1,3,5
  if (field.includes(',')) {
    const values = field.split(',').map(Number)
    return { type: 'list', values }
  }

  // Range: 1-5
  if (field.includes('-')) {
    const [lo, hi] = field.split('-').map(Number)
    const values: number[] = []
    for (let i = lo; i <= hi; i++) {
      values.push(i)
    }
    return { type: 'range', values }
  }

  // Single value
  return { type: 'value', values: [parseInt(field, 10)] }
}

function fieldMatches(field: CronField, value: number): boolean {
  if (field.type === 'wildcard') return true
  return field.values.includes(value)
}

export function parseCron(expression: string): CronSchedule {
  const parts = expression.trim().split(/\s+/)
  if (parts.length !== 5) {
    throw new Error(`Invalid cron expression: expected 5 fields, got ${parts.length}`)
  }
  return {
    minute: parseField(parts[0], 0, 59),
    hour: parseField(parts[1], 0, 23),
    dayOfMonth: parseField(parts[2], 1, 31),
    month: parseField(parts[3], 1, 12),
    dayOfWeek: parseField(parts[4], 0, 6),
  }
}

// ============================================================================
// Next Run Computation
// ============================================================================

export function getNextRun(schedule: CronSchedule, from?: Date): Date {
  const start = from ? new Date(from) : new Date()
  // Start from the next minute
  start.setSeconds(0, 0)
  start.setMinutes(start.getMinutes() + 1)

  // Brute-force search up to 1 year ahead (safe for any valid cron)
  const maxIterations = 525_960 // ~366 days * 24 hours * 60 minutes
  const candidate = new Date(start)

  for (let i = 0; i < maxIterations; i++) {
    if (
      fieldMatches(schedule.month, candidate.getMonth() + 1) &&
      fieldMatches(schedule.dayOfMonth, candidate.getDate()) &&
      fieldMatches(schedule.dayOfWeek, candidate.getDay()) &&
      fieldMatches(schedule.hour, candidate.getHours()) &&
      fieldMatches(schedule.minute, candidate.getMinutes())
    ) {
      return candidate
    }
    candidate.setMinutes(candidate.getMinutes() + 1)
  }

  // Fallback: should not happen for valid crons
  return new Date(start.getTime() + 86_400_000)
}

// ============================================================================
// Scheduler
// ============================================================================

export function startScheduler(
  workflows: ScheduledWorkflow[],
  onTrigger: (id: string) => void
): { stop: () => void } {
  const state = new Map<string, { schedule: CronSchedule; lastRun: number }>()

  for (const wf of workflows) {
    state.set(wf.id, {
      schedule: parseCron(wf.cron),
      lastRun: wf.lastRun ?? 0,
    })
  }

  const check = () => {
    const now = Date.now()
    for (const [id, entry] of state) {
      const next = getNextRun(entry.schedule, new Date(entry.lastRun || now - 60_000))
      if (next.getTime() <= now) {
        entry.lastRun = now
        onTrigger(id)
      }
    }
  }

  // Check immediately, then every 60 seconds
  check()
  const interval = setInterval(check, 60_000)

  return {
    stop: () => clearInterval(interval),
  }
}

// ============================================================================
// Human-Readable Formatting
// ============================================================================

const WEEKDAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday']

function pad2(n: number): string {
  return n.toString().padStart(2, '0')
}

function formatTime(hour: number, minute: number): string {
  const ampm = hour >= 12 ? 'PM' : 'AM'
  const h12 = hour === 0 ? 12 : hour > 12 ? hour - 12 : hour
  return `${h12}:${pad2(minute)} ${ampm}`
}

export function formatCronHuman(expression: string): string {
  try {
    const schedule = parseCron(expression)
    const { minute, hour, dayOfMonth, month, dayOfWeek } = schedule

    // Every minute: * * * * *
    if (
      minute.type === 'wildcard' &&
      hour.type === 'wildcard' &&
      dayOfMonth.type === 'wildcard' &&
      month.type === 'wildcard' &&
      dayOfWeek.type === 'wildcard'
    ) {
      return 'Every minute'
    }

    // Every N minutes: */N * * * *
    if (
      minute.type === 'step' &&
      hour.type === 'wildcard' &&
      dayOfMonth.type === 'wildcard' &&
      month.type === 'wildcard' &&
      dayOfWeek.type === 'wildcard'
    ) {
      const step = minute.values.length > 1 ? minute.values[1] - minute.values[0] : 0
      if (step > 0) return `Every ${step} minutes`
    }

    // Specific minute every hour: N * * * *
    if (
      minute.type === 'value' &&
      hour.type === 'wildcard' &&
      dayOfMonth.type === 'wildcard' &&
      month.type === 'wildcard' &&
      dayOfWeek.type === 'wildcard'
    ) {
      return `Every hour at :${pad2(minute.values[0])}`
    }

    // Daily at specific time: M H * * *
    if (
      minute.type === 'value' &&
      hour.type === 'value' &&
      dayOfMonth.type === 'wildcard' &&
      month.type === 'wildcard' &&
      dayOfWeek.type === 'wildcard'
    ) {
      return `Every day at ${formatTime(hour.values[0], minute.values[0])}`
    }

    // Weekly on specific day: M H * * D
    if (
      minute.type === 'value' &&
      hour.type === 'value' &&
      dayOfMonth.type === 'wildcard' &&
      month.type === 'wildcard' &&
      dayOfWeek.type === 'value'
    ) {
      return `Every ${WEEKDAYS[dayOfWeek.values[0]]} at ${formatTime(hour.values[0], minute.values[0])}`
    }

    // Monthly on specific day: M H D * *
    if (
      minute.type === 'value' &&
      hour.type === 'value' &&
      dayOfMonth.type === 'value' &&
      month.type === 'wildcard' &&
      dayOfWeek.type === 'wildcard'
    ) {
      const d = dayOfMonth.values[0]
      const suffix = d === 1 ? 'st' : d === 2 ? 'nd' : d === 3 ? 'rd' : 'th'
      return `Monthly on the ${d}${suffix} at ${formatTime(hour.values[0], minute.values[0])}`
    }

    return expression
  } catch {
    return expression
  }
}
