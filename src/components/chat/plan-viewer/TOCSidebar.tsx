import { Check, ChevronLeft, ChevronRight } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { PlanStep, PlanSummary } from '../../../types/rust-ipc'
import { ACTION_CONFIG, PLAN_ACCENT } from './types'

export const TOCSidebar: Component<{
  steps: PlanStep[]
  activeStepId: string | null
  collapsed: boolean
  planHistory: PlanSummary[]
  onScrollTo: (stepId: string) => void
  onToggleCollapse: () => void
  onLoadPlan: (filename: string) => void
}> = (props) => {
  const [activeTab, setActiveTab] = createSignal<'contents' | 'versions'>('contents')

  return (
    <Show
      when={!props.collapsed}
      fallback={
        <button
          type="button"
          onClick={() => props.onToggleCollapse()}
          class="flex-shrink-0 flex items-center justify-center border-r transition-colors"
          style={{
            width: '28px',
            background: 'var(--surface)',
            'border-color': 'var(--border-subtle)',
            color: 'var(--text-muted)',
          }}
          title="Show sidebar"
        >
          <ChevronRight class="w-3.5 h-3.5" />
        </button>
      }
    >
      <aside
        class="flex flex-col h-full border-r flex-shrink-0 overflow-hidden"
        style={{
          width: '240px',
          'min-width': '240px',
          background: 'var(--surface)',
          'border-color': 'var(--border-subtle)',
        }}
      >
        {/* Header with tabs */}
        <div
          class="flex items-center justify-between px-3 py-2 border-b flex-shrink-0"
          style={{ 'border-color': 'var(--border-subtle)' }}
        >
          <div class="flex items-center gap-1">
            <button
              type="button"
              onClick={() => setActiveTab('contents')}
              class="px-2 py-1 rounded text-[10px] font-semibold tracking-widest uppercase transition-colors"
              style={{
                color: activeTab() === 'contents' ? 'var(--text-primary)' : 'var(--text-muted)',
                background: activeTab() === 'contents' ? 'var(--alpha-white-5)' : 'transparent',
              }}
            >
              Contents
            </button>
            <button
              type="button"
              onClick={() => setActiveTab('versions')}
              class="px-2 py-1 rounded text-[10px] font-semibold tracking-widest uppercase transition-colors"
              style={{
                color: activeTab() === 'versions' ? 'var(--text-primary)' : 'var(--text-muted)',
                background: activeTab() === 'versions' ? 'var(--alpha-white-5)' : 'transparent',
              }}
            >
              Versions
            </button>
          </div>
          <button
            type="button"
            onClick={() => props.onToggleCollapse()}
            class="p-1 rounded transition-colors"
            style={{ color: 'var(--text-muted)' }}
            title="Collapse sidebar"
          >
            <ChevronLeft class="w-3.5 h-3.5" />
          </button>
        </div>

        {/* Contents tab */}
        <Show when={activeTab() === 'contents'}>
          <nav class="flex-1 overflow-y-auto py-2">
            <For each={props.steps}>
              {(step, i) => {
                const action = () => ACTION_CONFIG[step.action]
                return (
                  <button
                    type="button"
                    onClick={() => props.onScrollTo(step.id)}
                    class="w-full text-left flex items-center gap-2 px-3 py-2 transition-colors"
                    style={{
                      background:
                        props.activeStepId === step.id ? 'var(--alpha-white-5)' : 'transparent',
                      'border-left':
                        props.activeStepId === step.id
                          ? `2px solid ${PLAN_ACCENT}`
                          : '2px solid transparent',
                    }}
                  >
                    <span
                      class="flex-shrink-0 w-5 h-5 rounded-full flex items-center justify-center text-[10px] font-bold"
                      style={{
                        background: step.approved ? 'rgba(34, 197, 94, 0.15)' : action().bg,
                        color: step.approved ? '#22C55E' : action().text,
                      }}
                    >
                      <Show when={step.approved} fallback={i() + 1}>
                        <Check class="w-3 h-3" />
                      </Show>
                    </span>
                    <span
                      class="text-[12px] leading-tight truncate"
                      style={{
                        color:
                          props.activeStepId === step.id
                            ? 'var(--text-primary)'
                            : 'var(--text-secondary)',
                      }}
                    >
                      {step.description}
                    </span>
                  </button>
                )
              }}
            </For>
          </nav>

          {/* History section */}
          <Show when={props.planHistory.length > 0}>
            <div class="border-t px-3 py-2" style={{ 'border-color': 'var(--border-subtle)' }}>
              <span
                class="text-[10px] font-semibold tracking-widest uppercase block mb-2"
                style={{ color: 'var(--text-muted)' }}
              >
                History
              </span>
              <div class="space-y-1 max-h-[150px] overflow-y-auto">
                <For each={props.planHistory}>
                  {(entry) => (
                    <button
                      type="button"
                      onClick={() => props.onLoadPlan(entry.filename)}
                      class="w-full text-left rounded px-2 py-1.5 transition-colors text-[11px]"
                      style={{ color: 'var(--text-secondary)' }}
                      title={entry.summary}
                    >
                      <span class="block truncate font-medium">
                        {entry.codename || entry.summary.slice(0, 30)}
                      </span>
                      <span class="block text-[10px] mt-0.5" style={{ color: 'var(--text-muted)' }}>
                        {entry.stepCount} steps
                      </span>
                    </button>
                  )}
                </For>
              </div>
            </div>
          </Show>
        </Show>

        {/* Versions tab */}
        <Show when={activeTab() === 'versions'}>
          <div class="flex-1 overflow-y-auto p-3">
            <Show
              when={props.planHistory.length > 0}
              fallback={
                <div class="text-center py-8">
                  <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                    No saved versions yet.
                    <br />
                    Versions are saved when plans are approved or refined.
                  </span>
                </div>
              }
            >
              <div class="space-y-1">
                <For each={props.planHistory}>
                  {(entry, index) => (
                    <button
                      type="button"
                      onClick={() => props.onLoadPlan(entry.filename)}
                      class="w-full text-left p-2 rounded-lg border transition-colors"
                      style={{
                        'border-color': 'var(--border-subtle)',
                        background: 'var(--alpha-white-3)',
                      }}
                    >
                      <div class="flex items-center justify-between mb-0.5">
                        <span
                          class="text-[11px] font-medium"
                          style={{ color: 'var(--text-primary)' }}
                        >
                          {entry.codename || `Version ${props.planHistory.length - index()}`}
                        </span>
                        <span class="text-[9px]" style={{ color: 'var(--text-muted)' }}>
                          {entry.created}
                        </span>
                      </div>
                      <span
                        class="text-[10px] truncate block"
                        style={{ color: 'var(--text-muted)' }}
                      >
                        {entry.summary.slice(0, 50)}
                        {entry.summary.length > 50 ? '...' : ''}
                      </span>
                      <span class="text-[9px]" style={{ color: 'var(--text-muted)' }}>
                        {entry.stepCount} steps
                      </span>
                    </button>
                  )}
                </For>
              </div>
            </Show>
          </div>
        </Show>
      </aside>
    </Show>
  )
}
