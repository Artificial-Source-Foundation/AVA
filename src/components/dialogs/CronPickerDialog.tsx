/**
 * Cron Picker Dialog
 *
 * Visual cron expression builder with presets and custom mode.
 * Shows a preview of the next 3 scheduled runs.
 */

import { Calendar, Clock } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, on, Show } from 'solid-js'
import { formatCronHuman, getNextRun, parseCron } from '../../services/workflow-scheduler'

// ============================================================================
// Types + Presets
// ============================================================================

interface CronPickerDialogProps {
  open: boolean
  onClose: () => void
  onSave: (cron: string) => void
  initialCron?: string
}

interface CronPreset {
  label: string
  value: string
}

const PRESETS: CronPreset[] = [
  { label: 'Every 5 minutes', value: '*/5 * * * *' },
  { label: 'Every hour', value: '0 * * * *' },
  { label: 'Every day at 9am', value: '0 9 * * *' },
  { label: 'Every Monday', value: '0 9 * * 1' },
  { label: 'Custom', value: '' },
]

const MINUTES = Array.from({ length: 60 }, (_, i) => i)
const HOURS = Array.from({ length: 24 }, (_, i) => i)
const DAYS_OF_MONTH = Array.from({ length: 31 }, (_, i) => i + 1)
const MONTHS = [
  'January',
  'February',
  'March',
  'April',
  'May',
  'June',
  'July',
  'August',
  'September',
  'October',
  'November',
  'December',
]
const WEEKDAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday']

// ============================================================================
// Component
// ============================================================================

const INPUT_CLASS =
  'px-2 py-1.5 text-xs rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none'

export const CronPickerDialog: Component<CronPickerDialogProps> = (props) => {
  const [selectedPreset, setSelectedPreset] = createSignal(0)
  const [customMode, setCustomMode] = createSignal(false)

  // Custom field signals
  const [minute, setMinute] = createSignal('0')
  const [hour, setHour] = createSignal('*')
  const [dom, setDom] = createSignal('*')
  const [month, setMonth] = createSignal('*')
  const [dow, setDow] = createSignal('*')

  // Sync initial cron when dialog opens
  createEffect(
    on(
      () => props.open,
      (open) => {
        if (!open) return
        const initial = props.initialCron
        if (initial) {
          const presetIdx = PRESETS.findIndex((p) => p.value === initial)
          if (presetIdx >= 0) {
            setSelectedPreset(presetIdx)
            setCustomMode(false)
          } else {
            setSelectedPreset(PRESETS.length - 1) // Custom
            setCustomMode(true)
            const parts = initial.trim().split(/\s+/)
            if (parts.length === 5) {
              setMinute(parts[0])
              setHour(parts[1])
              setDom(parts[2])
              setMonth(parts[3])
              setDow(parts[4])
            }
          }
        } else {
          setSelectedPreset(0)
          setCustomMode(false)
        }
      }
    )
  )

  const cronExpression = createMemo(() => {
    if (customMode()) {
      return `${minute()} ${hour()} ${dom()} ${month()} ${dow()}`
    }
    return PRESETS[selectedPreset()]?.value || '* * * * *'
  })

  const humanLabel = createMemo(() => formatCronHuman(cronExpression()))

  const nextRuns = createMemo(() => {
    try {
      const schedule = parseCron(cronExpression())
      const runs: Date[] = []
      let from = new Date()
      for (let i = 0; i < 3; i++) {
        const next = getNextRun(schedule, from)
        runs.push(next)
        from = next
      }
      return runs
    } catch {
      return []
    }
  })

  const handlePresetClick = (index: number) => {
    setSelectedPreset(index)
    if (index === PRESETS.length - 1) {
      setCustomMode(true)
    } else {
      setCustomMode(false)
    }
  }

  const handleSave = () => {
    const expr = cronExpression()
    if (!expr || expr.trim().split(/\s+/).length !== 5) return
    props.onSave(expr)
    props.onClose()
  }

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
          {/* Header */}
          <div class="flex items-center gap-2">
            <Clock class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Schedule Workflow</h3>
          </div>
          <p class="text-xs text-[var(--text-secondary)]">
            Choose how often this workflow should run automatically.
          </p>

          {/* Presets */}
          <div class="flex flex-wrap gap-1.5">
            <For each={PRESETS}>
              {(preset, i) => (
                <button
                  type="button"
                  onClick={() => handlePresetClick(i())}
                  class="px-2.5 py-1 text-xs rounded-[var(--radius-md)] border transition-colors"
                  classList={{
                    'bg-[var(--accent)] text-white border-[var(--accent)]':
                      selectedPreset() === i(),
                    'bg-[var(--surface-sunken)] text-[var(--text-secondary)] border-[var(--border-subtle)] hover:border-[var(--accent)]':
                      selectedPreset() !== i(),
                  }}
                >
                  {preset.label}
                </button>
              )}
            </For>
          </div>

          {/* Custom mode: 5 fields */}
          <Show when={customMode()}>
            <div class="grid grid-cols-5 gap-2">
              {/* Minute */}
              <div class="space-y-1">
                <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                  Minute
                </span>
                <select
                  class={`${INPUT_CLASS} w-full`}
                  value={minute()}
                  onChange={(e) => setMinute(e.currentTarget.value)}
                >
                  <option value="*">*</option>
                  <option value="*/5">*/5</option>
                  <option value="*/10">*/10</option>
                  <option value="*/15">*/15</option>
                  <option value="*/30">*/30</option>
                  <For each={MINUTES}>{(m) => <option value={String(m)}>{m}</option>}</For>
                </select>
              </div>

              {/* Hour */}
              <div class="space-y-1">
                <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                  Hour
                </span>
                <select
                  class={`${INPUT_CLASS} w-full`}
                  value={hour()}
                  onChange={(e) => setHour(e.currentTarget.value)}
                >
                  <option value="*">*</option>
                  <For each={HOURS}>{(h) => <option value={String(h)}>{h}</option>}</For>
                </select>
              </div>

              {/* Day of Month */}
              <div class="space-y-1">
                <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                  Day
                </span>
                <select
                  class={`${INPUT_CLASS} w-full`}
                  value={dom()}
                  onChange={(e) => setDom(e.currentTarget.value)}
                >
                  <option value="*">*</option>
                  <For each={DAYS_OF_MONTH}>{(d) => <option value={String(d)}>{d}</option>}</For>
                </select>
              </div>

              {/* Month */}
              <div class="space-y-1">
                <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                  Month
                </span>
                <select
                  class={`${INPUT_CLASS} w-full`}
                  value={month()}
                  onChange={(e) => setMonth(e.currentTarget.value)}
                >
                  <option value="*">*</option>
                  <For each={MONTHS}>
                    {(m, i) => <option value={String(i() + 1)}>{m.slice(0, 3)}</option>}
                  </For>
                </select>
              </div>

              {/* Day of Week */}
              <div class="space-y-1">
                <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                  Weekday
                </span>
                <select
                  class={`${INPUT_CLASS} w-full`}
                  value={dow()}
                  onChange={(e) => setDow(e.currentTarget.value)}
                >
                  <option value="*">*</option>
                  <For each={WEEKDAYS}>
                    {(d, i) => <option value={String(i())}>{d.slice(0, 3)}</option>}
                  </For>
                </select>
              </div>
            </div>

            {/* Raw expression display */}
            <div class="px-2 py-1.5 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] text-xs font-mono text-[var(--text-secondary)]">
              {cronExpression()}
            </div>
          </Show>

          {/* Human-readable description */}
          <div class="text-xs text-[var(--text-primary)] font-medium">{humanLabel()}</div>

          {/* Next 3 runs preview */}
          <Show when={nextRuns().length > 0}>
            <div class="space-y-1">
              <div class="flex items-center gap-1.5 text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                <Calendar class="w-3 h-3" />
                Next 3 runs
              </div>
              <div class="space-y-0.5">
                <For each={nextRuns()}>
                  {(run) => (
                    <div class="text-[11px] text-[var(--text-secondary)] pl-4">
                      {run.toLocaleString(undefined, {
                        weekday: 'short',
                        month: 'short',
                        day: 'numeric',
                        hour: '2-digit',
                        minute: '2-digit',
                      })}
                    </div>
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Actions */}
          <div class="flex gap-2 justify-end pt-1">
            <button
              type="button"
              onClick={props.onClose}
              class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={handleSave}
              class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
            >
              Save Schedule
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
