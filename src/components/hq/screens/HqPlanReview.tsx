import {
  Check,
  Copy,
  MessageSquare,
  MousePointer2,
  Pen,
  ShieldCheck,
  Strikethrough,
  Tag,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { PhaseExecution, TaskComplexity } from '../../../types/hq'

type ViewMode = 'phases' | 'list' | 'timeline'

function complexityColor(c: TaskComplexity): string {
  if (c === 'simple') return 'var(--success)'
  if (c === 'medium') return 'var(--warning)'
  return 'var(--error)'
}

function executionTag(e: PhaseExecution): { label: string; color: string } {
  return e === 'parallel'
    ? { label: 'parallel', color: '#8b5cf6' }
    : { label: 'sequential', color: '#f59e0b' }
}

const HqPlanReview: Component = () => {
  const { plan, approveCurrentPlan, rejectCurrentPlan } = useHq()
  const [viewMode, setViewMode] = createSignal<ViewMode>('phases')
  const [activeTool] = createSignal<'select' | 'markup'>('select')

  const p = () => plan()

  const handleApprove = (): void => {
    void approveCurrentPlan()
  }

  const handleReject = (): void => {
    void rejectCurrentPlan('Please revise the plan.')
  }

  return (
    <Show
      when={p()}
      fallback={
        <div class="flex items-center justify-center h-full" style={{ color: 'var(--text-muted)' }}>
          No plan available
        </div>
      }
    >
      {(currentPlan) => (
        <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
          {/* Header */}
          <div
            class="flex items-center justify-between shrink-0 px-6 h-14"
            style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
          >
            <div class="flex items-center gap-3">
              <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                {currentPlan().title}
              </span>
              <span
                class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                style={{ color: 'var(--warning)', 'background-color': 'rgba(234,179,8,0.15)' }}
              >
                {currentPlan().status === 'awaiting-approval'
                  ? 'Awaiting Approval'
                  : currentPlan().status}
              </span>
            </div>
            <div class="flex items-center gap-2">
              {/* View toggle */}
              <div
                class="flex items-center rounded-md overflow-hidden"
                style={{ border: '1px solid var(--border-subtle)' }}
              >
                <For each={['phases', 'list', 'timeline'] as ViewMode[]}>
                  {(mode) => (
                    <button
                      type="button"
                      class="px-2.5 h-7 text-[11px] font-medium"
                      style={{
                        'background-color':
                          viewMode() === mode ? 'var(--accent)' : 'var(--surface)',
                        color: viewMode() === mode ? 'white' : 'var(--text-muted)',
                      }}
                      onClick={() => setViewMode(mode)}
                    >
                      {mode.charAt(0).toUpperCase() + mode.slice(1)}
                    </button>
                  )}
                </For>
              </div>
              <button
                type="button"
                class="flex items-center gap-1.5 h-8 px-3.5 rounded-md text-xs font-semibold"
                style={{ 'background-color': 'var(--success)', color: 'white' }}
                onClick={handleApprove}
              >
                <Check size={14} />
                Approve & Run
              </button>
              <button
                type="button"
                class="flex items-center gap-1.5 h-8 px-3.5 rounded-md text-xs font-semibold"
                style={{ 'background-color': 'var(--error)', color: 'white' }}
                onClick={handleReject}
              >
                <X size={14} />
                Request Changes
              </button>
            </div>
          </div>

          {/* Annotation toolbar */}
          <div
            class="flex items-center justify-between shrink-0 px-6 h-10"
            style={{
              'border-bottom': '1px solid var(--border-subtle)',
              'background-color': 'var(--surface)',
            }}
          >
            <div class="flex items-center gap-1">
              <button
                type="button"
                class="flex items-center gap-1 px-2 h-7 rounded text-[11px] font-medium"
                style={{
                  'background-color':
                    activeTool() === 'select' ? 'rgba(139,92,246,0.2)' : 'transparent',
                  color: activeTool() === 'select' ? '#8b5cf6' : 'var(--text-muted)',
                }}
              >
                <MousePointer2 size={12} />
                Select
              </button>
              <button
                type="button"
                class="flex items-center gap-1 px-2 h-7 rounded text-[11px] font-medium"
                style={{
                  'background-color':
                    activeTool() === 'markup' ? 'rgba(139,92,246,0.2)' : 'transparent',
                  color: activeTool() === 'markup' ? '#8b5cf6' : 'var(--text-muted)',
                }}
              >
                <Pen size={12} />
                Markup
              </button>
              <div class="w-px h-5 mx-1" style={{ 'background-color': 'var(--border-subtle)' }} />
              <button
                type="button"
                class="flex items-center justify-center w-7 h-7 rounded"
                title="Comment"
              >
                <MessageSquare size={13} style={{ color: 'var(--text-muted)' }} />
              </button>
              <button
                type="button"
                class="flex items-center justify-center w-7 h-7 rounded"
                title="Strikethrough"
              >
                <Strikethrough size={13} style={{ color: 'var(--text-muted)' }} />
              </button>
              <button
                type="button"
                class="flex items-center justify-center w-7 h-7 rounded"
                title="Tag"
              >
                <Tag size={13} style={{ color: 'var(--text-muted)' }} />
              </button>
            </div>
            <div class="flex items-center gap-1">
              <button
                type="button"
                class="flex items-center gap-1 px-2 h-7 rounded text-[11px] font-medium"
                style={{ color: 'var(--text-muted)' }}
              >
                <MessageSquare size={12} />
                Global comment
              </button>
              <button
                type="button"
                class="flex items-center gap-1 px-2 h-7 rounded text-[11px] font-medium"
                style={{ color: 'var(--text-muted)' }}
              >
                <Copy size={12} />
                Copy plan
              </button>
            </div>
          </div>

          {/* Content */}
          <div
            class="flex-1 overflow-y-auto px-6 py-5"
            style={{ display: 'flex', 'flex-direction': 'column', gap: '20px' }}
          >
            {/* Director description */}
            <p class="text-sm leading-relaxed" style={{ color: 'var(--text-secondary)' }}>
              {currentPlan().directorDescription}
            </p>

            {/* Phases */}
            <div class="flex flex-col gap-4">
              <For each={currentPlan().phases}>
                {(phase) => {
                  const tag = executionTag(phase.execution)
                  const isParallel = phase.execution === 'parallel'
                  return (
                    <div
                      class="rounded-lg p-4"
                      style={{
                        'background-color': 'var(--surface)',
                        border: '1px solid var(--border-subtle)',
                      }}
                    >
                      {/* Phase header */}
                      <div class="flex items-center gap-2.5 mb-3">
                        <span
                          class="flex items-center justify-center w-6 h-6 rounded-full text-[10px] font-bold"
                          style={{ 'background-color': 'var(--accent)', color: 'white' }}
                        >
                          {phase.number}
                        </span>
                        <span
                          class="text-sm font-semibold"
                          style={{ color: 'var(--text-primary)' }}
                        >
                          {phase.name}
                        </span>
                        <span
                          class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                          style={{ color: tag.color, 'background-color': `${tag.color}22` }}
                        >
                          {tag.label}
                        </span>
                      </div>
                      <p class="text-xs mb-3" style={{ color: 'var(--text-muted)' }}>
                        {phase.description}
                      </p>

                      {/* Tasks - parallel side-by-side, sequential stacked */}
                      <div
                        class={isParallel ? 'flex gap-3' : 'flex flex-col gap-3'}
                        style={isParallel ? { 'flex-wrap': 'wrap' } : {}}
                      >
                        <For each={phase.tasks}>
                          {(task) => (
                            <div
                              class="rounded-md p-3"
                              style={{
                                'background-color': 'var(--background)',
                                border: '1px solid var(--border-subtle)',
                                flex: isParallel ? '1 1 0' : undefined,
                                'min-width': isParallel ? '200px' : undefined,
                              }}
                            >
                              <div class="flex items-center gap-2 mb-1.5">
                                <input type="checkbox" class="w-3.5 h-3.5 rounded" />
                                <span
                                  class="text-xs font-semibold flex-1"
                                  style={{ color: 'var(--text-primary)' }}
                                >
                                  {task.title}
                                </span>
                              </div>
                              <div class="flex items-center gap-1.5 flex-wrap">
                                <span
                                  class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                                  style={{
                                    color: 'var(--accent)',
                                    'background-color': 'rgba(139,92,246,0.15)',
                                  }}
                                >
                                  {task.domain}
                                </span>
                                <span
                                  class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                                  style={{
                                    color: complexityColor(task.complexity),
                                    'background-color': `${complexityColor(task.complexity)}22`,
                                  }}
                                >
                                  {task.complexity}
                                </span>
                                <Show when={task.assigneeName}>
                                  <span
                                    class="text-[9px] font-medium px-1.5 py-0.5 rounded"
                                    style={{
                                      color: 'var(--text-secondary)',
                                      'background-color': 'var(--surface)',
                                    }}
                                  >
                                    {task.assigneeName} ({task.assigneeModel})
                                  </span>
                                </Show>
                              </div>

                              {/* Expanded steps + file hints */}
                              <Show when={task.expanded && task.steps.length > 0}>
                                <div
                                  class="mt-3 pt-2.5 flex flex-col gap-1.5"
                                  style={{ 'border-top': '1px solid var(--border-subtle)' }}
                                >
                                  <For each={task.steps}>
                                    {(step, i) => (
                                      <div class="flex items-start gap-2">
                                        <span
                                          class="text-[10px] font-mono shrink-0 w-4 text-right"
                                          style={{ color: 'var(--text-muted)' }}
                                        >
                                          {i() + 1}.
                                        </span>
                                        <span
                                          class="text-[11px]"
                                          style={{ color: 'var(--text-secondary)' }}
                                        >
                                          {step}
                                        </span>
                                      </div>
                                    )}
                                  </For>
                                  <Show when={task.fileHints.length > 0}>
                                    <div class="flex flex-wrap gap-1 mt-1">
                                      <For each={task.fileHints}>
                                        {(f) => (
                                          <span
                                            class="text-[9px] font-mono px-1.5 py-0.5 rounded"
                                            style={{
                                              color: 'var(--text-muted)',
                                              'background-color': 'var(--surface)',
                                            }}
                                          >
                                            {f}
                                          </span>
                                        )}
                                      </For>
                                    </div>
                                  </Show>
                                </div>
                              </Show>
                            </div>
                          )}
                        </For>
                      </div>

                      {/* Review toggle */}
                      <div
                        class="flex items-center gap-2 mt-3 pt-2.5"
                        style={{ 'border-top': '1px solid var(--border-subtle)' }}
                      >
                        <ShieldCheck
                          size={14}
                          style={{
                            color: phase.reviewEnabled ? 'var(--warning)' : 'var(--text-muted)',
                          }}
                        />
                        <span
                          class="text-[11px] font-medium"
                          style={{
                            color: phase.reviewEnabled ? 'var(--warning)' : 'var(--text-muted)',
                          }}
                        >
                          {phase.reviewEnabled
                            ? `Review step assigned to ${phase.reviewAssignee ?? 'unassigned'}`
                            : 'Add review step'}
                        </span>
                      </div>
                    </div>
                  )
                }}
              </For>
            </div>
          </div>
        </div>
      )}
    </Show>
  )
}

export default HqPlanReview
