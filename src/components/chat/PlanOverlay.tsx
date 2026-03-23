/**
 * Full-Screen Plan Overlay — Plannotator Design
 *
 * 3-panel layout: TOC sidebar | Document card on grid canvas | Annotations panel
 * With floating selection toolbar, comment popovers, and annotation highlights.
 *
 * Design reference: Plannotator (backnotprop/plannotator)
 */

import {
  ArrowLeft,
  Check,
  ChevronLeft,
  ClipboardList,
  Copy,
  Download,
  FileCode,
  GitBranch,
  Link2,
  MessageCircle,
  MessageSquare,
  MousePointer2,
  Pencil,
  Settings,
  Square,
  Trash2,
  X,
  Zap,
} from 'lucide-solid'
import { type Component, createEffect, createSignal, For, onCleanup, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { type PlanAnnotation, usePlanOverlay } from '../../stores/planOverlayStore'
import type { PlanData, PlanStep, PlanStepAction, PlanSummary } from '../../types/rust-ipc'

// ============================================================================
// Constants
// ============================================================================

const PLAN_ACCENT = '#8B5CF6'
const PLAN_ACCENT_SUBTLE = 'rgba(139, 92, 246, 0.12)'

const ACTION_CONFIG: Record<
  PlanStepAction,
  { bg: string; text: string; border: string; label: string }
> = {
  research: {
    bg: 'rgba(6, 182, 212, 0.10)',
    text: '#06B6D4',
    border: 'rgba(6, 182, 212, 0.25)',
    label: 'Research',
  },
  implement: {
    bg: 'rgba(59, 130, 246, 0.10)',
    text: '#3B82F6',
    border: 'rgba(59, 130, 246, 0.25)',
    label: 'Implement',
  },
  test: {
    bg: 'rgba(34, 197, 94, 0.10)',
    text: '#22C55E',
    border: 'rgba(34, 197, 94, 0.25)',
    label: 'Test',
  },
  review: {
    bg: 'rgba(245, 158, 11, 0.10)',
    text: '#F59E0B',
    border: 'rgba(245, 158, 11, 0.25)',
    label: 'Review',
  },
}

// ============================================================================
// Helpers
// ============================================================================

/** Format a plan as markdown text for copy/download */
function formatPlanMarkdown(plan: PlanData): string {
  return [
    `# ${plan.codename ? `${plan.codename} — ` : ''}${plan.summary}`,
    '',
    ...plan.steps.map(
      (s, i) =>
        `${i + 1}. [${s.action.toUpperCase()}] ${s.description}${s.files.length ? `\n   Files: ${s.files.join(', ')}` : ''}`
    ),
  ].join('\n')
}

/** Parse a plan markdown file back into PlanData */
function parsePlanMarkdown(content: string): PlanData | null {
  const lines = content.split('\n')
  let codename: string | undefined
  let summary = ''
  const steps: PlanStep[] = []

  let inFrontmatter = false
  let frontmatterDone = false
  for (const line of lines) {
    if (line.trim() === '---') {
      if (!inFrontmatter) {
        inFrontmatter = true
        continue
      } else {
        inFrontmatter = false
        frontmatterDone = true
        continue
      }
    }
    if (inFrontmatter) {
      const codenameMatch = line.match(/^codename:\s*(.+)/)
      if (codenameMatch) codename = codenameMatch[1].trim()
      const summaryMatch = line.match(/^summary:\s*"?(.+?)"?\s*$/)
      if (summaryMatch) summary = summaryMatch[1]
      continue
    }
    if (!frontmatterDone && !inFrontmatter) continue

    const stepMatch = line.match(/^(?:###\s+|(\d+)\.\s+)\[(\w+)]\s+(.+)/)
    if (stepMatch) {
      const action = (stepMatch[2].toLowerCase() as PlanStepAction) || 'implement'
      steps.push({
        id: `step-${steps.length + 1}`,
        description: stepMatch[3],
        files: [],
        action: ['research', 'implement', 'test', 'review'].includes(action) ? action : 'implement',
        dependsOn: [],
        approved: false,
      })
    }
    const filesMatch = line.match(/^\s+Files:\s*(.+)/)
    if (filesMatch && steps.length > 0) {
      steps[steps.length - 1].files = filesMatch[1].split(',').map((f) => f.trim())
    }
  }

  if (!summary && steps.length === 0) return null
  return { summary, steps, estimatedTurns: steps.length, codename }
}

function generateId(): string {
  return `ann-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
}

// ============================================================================
// Sub-components
// ============================================================================

/** Floating toolbar that appears when user selects text inside the document */
const SelectionToolbar: Component<{
  text: string
  top: number
  left: number
  onCopy: () => void
  onDelete: () => void
  onComment: () => void
  onQuickLabel: () => void
  onClose: () => void
}> = (props) => {
  return (
    <div
      class="fixed z-[100] flex items-center gap-0.5 rounded-lg border p-1"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        background: 'var(--surface-raised)',
        'border-color': 'var(--border-subtle)',
        'box-shadow': '0 10px 25px -5px rgba(0,0,0,0.25), 0 8px 10px -6px rgba(0,0,0,0.15)',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      <button
        type="button"
        onClick={() => props.onCopy()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: 'var(--text-secondary)' }}
        title="Copy selected text"
      >
        <Copy class="w-3.5 h-3.5" />
        Copy
      </button>

      <div class="w-px h-5 mx-0.5" style={{ background: 'var(--border-subtle)' }} />

      <button
        type="button"
        onClick={() => props.onDelete()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: '#EF4444' }}
        title="Mark for deletion"
      >
        <Trash2 class="w-3.5 h-3.5" />
        Delete
      </button>

      <button
        type="button"
        onClick={() => props.onComment()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: PLAN_ACCENT }}
        title="Add comment"
      >
        <MessageSquare class="w-3.5 h-3.5" />
        Comment
      </button>

      <button
        type="button"
        onClick={() => props.onQuickLabel()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: '#F59E0B' }}
        title="Quick label"
      >
        <Zap class="w-3.5 h-3.5" />
      </button>

      <button
        type="button"
        onClick={() => props.onClose()}
        class="flex items-center px-1.5 py-1.5 rounded-md text-[12px] transition-colors"
        style={{ color: 'var(--text-muted)' }}
        title="Dismiss"
      >
        <X class="w-3.5 h-3.5" />
      </button>
    </div>
  )
}

/** Comment popover anchored near selected text */
const CommentPopover: Component<{
  contextText: string
  top: number
  left: number
  onSave: (comment: string) => void
  onCancel: () => void
}> = (props) => {
  const [text, setText] = createSignal('')

  return (
    <div
      class="fixed z-[110] rounded-xl border w-[320px]"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        background: 'var(--surface-raised)',
        'border-color': 'var(--border-subtle)',
        'box-shadow': '0 20px 40px -10px rgba(0,0,0,0.3)',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      {/* Context quote */}
      <div
        class="px-4 pt-3 pb-2 text-[11px] italic border-b"
        style={{
          color: 'var(--text-muted)',
          'border-color': 'var(--border-subtle)',
          'max-height': '60px',
          overflow: 'hidden',
          'text-overflow': 'ellipsis',
        }}
      >
        "{props.contextText.slice(0, 120)}
        {props.contextText.length > 120 ? '...' : ''}"
      </div>

      {/* Textarea */}
      <div class="p-3">
        <textarea
          value={text()}
          onInput={(e) => setText(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
              e.preventDefault()
              const val = text().trim()
              if (val) props.onSave(val)
            }
            if (e.key === 'Escape') {
              e.preventDefault()
              props.onCancel()
            }
          }}
          ref={(el) => setTimeout(() => el.focus(), 50)}
          placeholder="Add a comment..."
          rows={3}
          class="w-full text-[13px] rounded-lg border px-3 py-2 outline-none resize-none"
          style={{
            color: 'var(--text-primary)',
            background: 'var(--alpha-white-3)',
            'border-color': 'var(--border-subtle)',
          }}
        />
        <div class="flex items-center justify-between mt-2">
          <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
            Ctrl+Enter to save
          </span>
          <div class="flex items-center gap-2">
            <button
              type="button"
              onClick={() => props.onCancel()}
              class="text-[12px] px-3 py-1 rounded-md transition-colors"
              style={{ color: 'var(--text-muted)' }}
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={() => {
                const val = text().trim()
                if (val) props.onSave(val)
              }}
              class="text-[12px] px-3 py-1 rounded-md font-medium transition-colors"
              style={{ color: '#fff', background: PLAN_ACCENT }}
            >
              Save
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

/** Right sidebar showing annotations */
const AnnotationsPanel: Component<{
  annotations: PlanAnnotation[]
  focusedId: string | null
  onFocus: (id: string) => void
  onRemove: (id: string) => void
}> = (props) => {
  return (
    <aside
      class="flex flex-col h-full border-l flex-shrink-0 overflow-hidden"
      style={{
        width: '288px',
        background: 'var(--surface)',
        'border-color': 'var(--border-subtle)',
      }}
    >
      {/* Header */}
      <div
        class="flex items-center gap-2 px-4 py-3 border-b flex-shrink-0"
        style={{ 'border-color': 'var(--border-subtle)' }}
      >
        <span
          class="text-[11px] font-semibold tracking-widest uppercase"
          style={{ color: 'var(--text-muted)' }}
        >
          Annotations
        </span>
        <Show when={props.annotations.length > 0}>
          <span
            class="inline-flex items-center justify-center min-w-[18px] h-[18px] rounded-full text-[10px] font-bold px-1"
            style={{ background: PLAN_ACCENT_SUBTLE, color: PLAN_ACCENT }}
          >
            {props.annotations.length}
          </span>
        </Show>
      </div>

      {/* Content */}
      <div class="flex-1 overflow-y-auto p-3 space-y-2">
        <Show
          when={props.annotations.length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full gap-3 text-center px-4">
              <div
                class="w-10 h-10 rounded-full flex items-center justify-center"
                style={{ background: 'var(--alpha-white-5)' }}
              >
                <MessageCircle class="w-5 h-5" style={{ color: 'var(--text-muted)' }} />
              </div>
              <span class="text-[12px]" style={{ color: 'var(--text-muted)' }}>
                Select text to add annotations
              </span>
            </div>
          }
        >
          <For each={props.annotations}>
            {(ann) => (
              <article
                onClick={() => props.onFocus(ann.id)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') props.onFocus(ann.id)
                }}
                class="w-full text-left rounded-lg border p-3 transition-all cursor-pointer"
                style={{
                  background:
                    props.focusedId === ann.id
                      ? 'rgba(59, 130, 246, 0.08)'
                      : 'var(--alpha-white-3)',
                  'border-color':
                    props.focusedId === ann.id ? 'rgba(59, 130, 246, 0.3)' : 'var(--border-subtle)',
                }}
              >
                <div class="flex items-center justify-between mb-1.5">
                  <span
                    class="inline-flex items-center px-1.5 py-0.5 rounded text-[9px] font-semibold uppercase tracking-wider"
                    style={{
                      background:
                        ann.type === 'deletion'
                          ? 'rgba(239, 68, 68, 0.12)'
                          : ann.type === 'comment'
                            ? 'rgba(234, 179, 8, 0.12)'
                            : 'rgba(139, 92, 246, 0.12)',
                      color:
                        ann.type === 'deletion'
                          ? '#EF4444'
                          : ann.type === 'comment'
                            ? '#EAB308'
                            : PLAN_ACCENT,
                    }}
                  >
                    {ann.type === 'deletion'
                      ? 'Deletion'
                      : ann.type === 'comment'
                        ? 'Comment'
                        : 'Global'}
                  </span>
                  <button
                    type="button"
                    onClick={(e) => {
                      e.stopPropagation()
                      props.onRemove(ann.id)
                    }}
                    class="p-0.5 rounded transition-colors"
                    style={{ color: 'var(--text-muted)' }}
                    title="Remove annotation"
                  >
                    <X class="w-3 h-3" />
                  </button>
                </div>
                <p
                  class="text-[11px] leading-relaxed mb-1"
                  style={{
                    color: 'var(--text-secondary)',
                    'text-decoration': ann.type === 'deletion' ? 'line-through' : 'none',
                    'text-decoration-color': ann.type === 'deletion' ? '#EF4444' : undefined,
                  }}
                >
                  {ann.originalText.slice(0, 80)}
                  {ann.originalText.length > 80 ? '...' : ''}
                </p>
                <Show when={ann.commentText}>
                  <p class="text-[11px] italic" style={{ color: 'var(--text-muted)' }}>
                    {ann.commentText}
                  </p>
                </Show>
              </article>
            )}
          </For>
        </Show>
      </div>
    </aside>
  )
}

/** Left sidebar with table of contents and plan history */
const TOCSidebar: Component<{
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
    <aside
      class="flex flex-col h-full border-r flex-shrink-0 overflow-hidden transition-all"
      style={{
        width: props.collapsed ? '0px' : '240px',
        'min-width': props.collapsed ? '0px' : '240px',
        background: 'var(--surface)',
        'border-color': 'var(--border-subtle)',
        opacity: props.collapsed ? '0' : '1',
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

      {/* Versions tab (placeholder) */}
      <Show when={activeTab() === 'versions'}>
        <div class="flex-1 flex items-center justify-center px-4">
          <span class="text-[12px] text-center" style={{ color: 'var(--text-muted)' }}>
            Version history coming soon
          </span>
        </div>
      </Show>
    </aside>
  )
}

/** Floating mode toolbar above the document card */
const AnnotationToolstrip: Component<{
  activeMode: 'select' | 'markup' | 'comment' | 'shape' | 'quicklabel'
  onModeChange: (mode: 'select' | 'markup' | 'comment' | 'shape' | 'quicklabel') => void
}> = (props) => {
  return (
    <div class="flex items-center justify-center mb-4">
      <div
        class="inline-flex items-center gap-0.5 rounded-full border px-1 py-0.5"
        style={{
          background: 'var(--surface-raised)',
          'border-color': 'var(--border-subtle)',
          'box-shadow': '0 2px 8px rgba(0,0,0,0.12)',
        }}
      >
        {/* Group 1: Select + Settings */}
        <div class="flex items-center gap-0.5 px-1">
          <button
            type="button"
            onClick={() => props.onModeChange('select')}
            class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-full text-[11px] font-medium transition-colors"
            style={{
              background: props.activeMode === 'select' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.activeMode === 'select' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
          >
            <MousePointer2 class="w-3.5 h-3.5" />
            Select
          </button>
          <button
            type="button"
            class="p-1.5 rounded-full transition-colors"
            style={{ color: 'var(--text-muted)' }}
            title="Settings"
            tabIndex={0}
          >
            <Settings class="w-3.5 h-3.5" />
          </button>
        </div>

        <div class="w-px h-5" style={{ background: 'var(--border-subtle)' }} />

        {/* Group 2: Markup + Comment + Shape + Quick Label */}
        <div class="flex items-center gap-0.5 px-1">
          <button
            type="button"
            onClick={() => props.onModeChange('markup')}
            class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-full text-[11px] font-medium transition-colors"
            style={{
              background: props.activeMode === 'markup' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.activeMode === 'markup' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
          >
            <Pencil class="w-3.5 h-3.5" />
            Markup
          </button>
          <button
            type="button"
            onClick={() => props.onModeChange('comment')}
            class="p-1.5 rounded-full transition-colors"
            style={{
              background: props.activeMode === 'comment' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.activeMode === 'comment' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
            title="Comment mode"
          >
            <MessageSquare class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => props.onModeChange('shape')}
            class="p-1.5 rounded-full transition-colors"
            style={{
              background: props.activeMode === 'shape' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.activeMode === 'shape' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
            title="Shape mode"
          >
            <Square class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => props.onModeChange('quicklabel')}
            class="p-1.5 rounded-full transition-colors"
            style={{
              background: props.activeMode === 'quicklabel' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.activeMode === 'quicklabel' ? '#F59E0B' : 'var(--text-muted)',
            }}
            title="Quick label"
          >
            <Zap class="w-3.5 h-3.5" />
          </button>
        </div>
      </div>
    </div>
  )
}

/** The centered document card that renders the plan as a readable document */
const PlanDocument: Component<{
  plan: PlanData
  annotations: PlanAnnotation[]
  onMouseUp: (e: MouseEvent) => void
  onGlobalComment: () => void
  onCopyPlan: () => void
  cardRef: (el: HTMLElement) => void
}> = (props) => {
  const approvedCount = () => props.plan.steps.filter((s) => s.approved).length
  const estimatedCost = () =>
    props.plan.estimatedBudgetUsd != null ? `~$${props.plan.estimatedBudgetUsd.toFixed(2)}` : null

  return (
    <article
      ref={props.cardRef}
      onMouseUp={(e) => props.onMouseUp(e)}
      class="relative mx-auto select-text"
      style={{
        'max-width': '880px',
        background: 'var(--surface)',
        'border-radius': '12px',
        'box-shadow': '0 20px 25px -5px rgba(0,0,0,0.15), 0 8px 10px -6px rgba(0,0,0,0.1)',
        border: '1px solid var(--border-subtle)',
        padding: '40px',
        animation: 'planCardIn 250ms ease-out',
      }}
    >
      {/* Top-right action links */}
      <div class="absolute top-4 right-4 flex items-center gap-3">
        <button
          type="button"
          onClick={() => props.onGlobalComment()}
          class="text-[11px] font-medium transition-colors"
          style={{ color: 'var(--text-muted)' }}
        >
          Global comment
        </button>
        <button
          type="button"
          onClick={() => props.onCopyPlan()}
          class="text-[11px] font-medium transition-colors"
          style={{ color: 'var(--text-muted)' }}
        >
          Copy plan
        </button>
      </div>

      {/* Codename badge */}
      <Show when={props.plan.codename}>
        <span
          class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold tracking-wider uppercase mb-3"
          style={{ background: PLAN_ACCENT_SUBTLE, color: PLAN_ACCENT }}
        >
          {props.plan.codename}
        </span>
      </Show>

      {/* Title */}
      <h1 class="text-[24px] font-bold leading-tight mb-3" style={{ color: 'var(--text-primary)' }}>
        {props.plan.summary}
      </h1>

      {/* Meta line */}
      <p class="text-[13px] mb-6" style={{ color: 'var(--text-muted)' }}>
        {props.plan.steps.length} steps
        {' \u00B7 '}~{props.plan.estimatedTurns} turns
        <Show when={estimatedCost()}>
          {' \u00B7 '}
          {estimatedCost()}
        </Show>
        <Show when={approvedCount() > 0}>
          {' \u00B7 '}
          <span style={{ color: '#22C55E' }}>
            {approvedCount()}/{props.plan.steps.length} approved
          </span>
        </Show>
      </p>

      {/* Divider */}
      <div class="mb-6" style={{ height: '1px', background: 'var(--border-subtle)' }} />

      {/* Steps rendered as document sections */}
      <div class="space-y-6">
        <For each={props.plan.steps}>
          {(step, i) => {
            const action = () => ACTION_CONFIG[step.action]
            const depLabels = () =>
              step.dependsOn.map((depId) => {
                const idx = props.plan.steps.findIndex((s) => s.id === depId)
                return idx >= 0 ? `Step ${idx + 1}` : depId
              })

            return (
              <section id={`plan-step-${step.id}`} data-step-id={step.id}>
                {/* Step heading */}
                <div class="flex items-center gap-3 mb-2">
                  <h3 class="text-[16px] font-semibold" style={{ color: 'var(--text-primary)' }}>
                    <span style={{ color: 'var(--text-muted)' }}>{i() + 1}.</span>{' '}
                    {step.description}
                  </h3>
                  <span
                    class="inline-flex items-center px-2 py-0.5 rounded-full text-[9px] font-semibold tracking-wider uppercase border flex-shrink-0"
                    style={{
                      background: action().bg,
                      color: action().text,
                      'border-color': action().border,
                    }}
                  >
                    {action().label}
                  </span>
                  <Show when={step.approved}>
                    <Check class="w-4 h-4 flex-shrink-0" style={{ color: '#22C55E' }} />
                  </Show>
                </div>

                {/* Files */}
                <Show when={step.files.length > 0}>
                  <ul class="mb-2 space-y-0.5">
                    <For each={step.files}>
                      {(file) => (
                        <li class="flex items-center gap-2 text-[12px]">
                          <FileCode
                            class="w-3 h-3 flex-shrink-0"
                            style={{ color: 'var(--text-muted)' }}
                          />
                          <code
                            class="px-1 rounded"
                            style={{
                              color: 'var(--text-secondary)',
                              background: 'var(--alpha-white-3)',
                              'font-family': 'var(--font-ui-mono)',
                              'font-size': '11px',
                            }}
                          >
                            {file}
                          </code>
                        </li>
                      )}
                    </For>
                  </ul>
                </Show>

                {/* Dependencies */}
                <Show when={step.dependsOn.length > 0}>
                  <p
                    class="flex items-center gap-1.5 text-[11px]"
                    style={{ color: 'var(--text-muted)' }}
                  >
                    <GitBranch class="w-3 h-3 flex-shrink-0" />
                    Depends on: {depLabels().join(', ')}
                  </p>
                </Show>
              </section>
            )
          }}
        </For>
      </div>
    </article>
  )
}

// ============================================================================
// Main Overlay
// ============================================================================

export const PlanOverlay: Component = () => {
  const {
    activePlan,
    isOpen,
    closePlan,
    executePlan,
    stepComments,
    annotations,
    addAnnotation,
    removeAnnotation,
  } = usePlanOverlay()
  const agent = useAgent()

  const [copied, setCopied] = createSignal(false)
  const [shareCopied, setShareCopied] = createSignal(false)
  const [tocCollapsed, setTocCollapsed] = createSignal(false)
  const [activeStepId, setActiveStepId] = createSignal<string | null>(null)
  const [planHistory, setPlanHistory] = createSignal<PlanSummary[]>([])
  const [toolMode, setToolMode] = createSignal<
    'select' | 'markup' | 'comment' | 'shape' | 'quicklabel'
  >('select')
  const [focusedAnnotationId, setFocusedAnnotationId] = createSignal<string | null>(null)

  // Selection toolbar state
  const [selectionToolbar, setSelectionToolbar] = createSignal<{
    text: string
    top: number
    left: number
  } | null>(null)

  // Comment popover state
  const [commentPopover, setCommentPopover] = createSignal<{
    text: string
    top: number
    left: number
  } | null>(null)

  // Global comment popover
  const [globalCommentOpen, setGlobalCommentOpen] = createSignal(false)

  // Fetch plan history when overlay opens
  createEffect(() => {
    if (isOpen()) {
      fetch('/api/plans')
        .then((r) => r.json())
        .then((plans: PlanSummary[]) => setPlanHistory(plans))
        .catch(() => {})
    }
  })

  /** Load a saved plan from history by filename. */
  const loadPlanFromHistory = (filename: string): void => {
    fetch(`/api/plans/${encodeURIComponent(filename)}`)
      .then((r) => {
        if (!r.ok) throw new Error('Not found')
        return r.text()
      })
      .then((content) => {
        const plan = parsePlanMarkdown(content)
        if (plan) {
          const { openPlan: openPlanFn } = usePlanOverlay()
          openPlanFn(plan)
        }
      })
      .catch(() => {})
  }

  /** Generate a shareable URL with the plan encoded in the hash. */
  const handleShare = (): void => {
    const plan = activePlan()
    if (!plan) return
    const json = JSON.stringify(plan)
    const encoded = btoa(unescape(encodeURIComponent(json)))
    const url = `${window.location.origin}${window.location.pathname}#plan=${encoded}`
    navigator.clipboard.writeText(url).then(() => {
      setShareCopied(true)
      setTimeout(() => setShareCopied(false), 2000)
    })
  }

  /** Collect step comments into a flat record for the backend. */
  const collectStepComments = (): Record<string, string> => {
    const comments = stepComments()
    const result: Record<string, string> = {}
    for (const [k, v] of Object.entries(comments)) {
      if (v) result[k] = v
    }
    return result
  }

  /** Build feedback string from annotations */
  const collectAnnotationFeedback = (): string => {
    const anns = annotations()
    if (anns.length === 0) return ''
    const parts: string[] = []
    for (const ann of anns) {
      if (ann.type === 'deletion') {
        parts.push(`[DELETE] "${ann.originalText}"`)
      } else if (ann.type === 'comment') {
        parts.push(`[COMMENT on "${ann.originalText.slice(0, 60)}..."] ${ann.commentText ?? ''}`)
      } else if (ann.type === 'global_comment') {
        parts.push(`[GLOBAL COMMENT] ${ann.commentText ?? ''}`)
      }
    }
    return parts.join('\n')
  }

  /** Approve and execute the plan via the agent backend. */
  const handleApprove = (mode: 'code' | 'praxis'): void => {
    const plan = activePlan()
    if (!plan) return
    agent.resolvePlan('approved', plan, undefined, collectStepComments())
    executePlan(mode)
  }

  /** Send rejection with annotations as feedback to the agent backend. */
  const handleReject = (): void => {
    const feedback = collectAnnotationFeedback()
    const comments = collectStepComments()
    const commentEntries = Object.entries(comments)
    const commentSuffix =
      commentEntries.length > 0
        ? `\n\nStep comments:\n${commentEntries.map(([id, c]) => `- Step ${id}: ${c}`).join('\n')}`
        : ''
    const fullFeedback = (feedback + commentSuffix).trim()
    if (!fullFeedback) {
      // If no annotations/comments, just close
      closePlan()
      return
    }
    agent.resolvePlan('rejected', undefined, fullFeedback, comments)
    closePlan()
  }

  const handleCopy = (): void => {
    const plan = activePlan()
    if (!plan) return
    const text = formatPlanMarkdown(plan)
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    })
  }

  const handleDownload = (): void => {
    const plan = activePlan()
    if (!plan) return
    const md = formatPlanMarkdown(plan)
    const blob = new Blob([md], { type: 'text/markdown' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `${plan.codename || 'plan'}.md`
    a.click()
    URL.revokeObjectURL(url)
  }

  /** Handle text selection inside the document card */
  const handleDocumentMouseUp = (): void => {
    const sel = window.getSelection()
    if (!sel || sel.isCollapsed || !sel.toString().trim()) {
      // Don't dismiss if clicking inside the toolbar/popover
      return
    }
    const range = sel.getRangeAt(0)
    const rect = range.getBoundingClientRect()
    setSelectionToolbar({
      text: sel.toString(),
      top: rect.top - 48,
      left: rect.left + rect.width / 2,
    })
  }

  /** Dismiss selection toolbar when clicking outside */
  const handleDocumentClick = (e: MouseEvent): void => {
    const target = e.target as HTMLElement
    // Don't dismiss if clicking toolbar or popover
    if (target.closest('[data-selection-toolbar]') || target.closest('[data-comment-popover]')) {
      return
    }
    // Only dismiss if no text is selected
    const sel = window.getSelection()
    if (!sel || sel.isCollapsed) {
      setSelectionToolbar(null)
    }
  }

  /** Scroll to a step in the document */
  const scrollToStep = (stepId: string): void => {
    setActiveStepId(stepId)
    const el = document.getElementById(`plan-step-${stepId}`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    }
  }

  // Keyboard shortcuts
  createEffect(() => {
    if (!isOpen()) return
    const handleKeyDown = (e: KeyboardEvent): void => {
      if (e.key === 'Escape') {
        e.preventDefault()
        e.stopPropagation()
        if (commentPopover()) {
          setCommentPopover(null)
          return
        }
        if (selectionToolbar()) {
          setSelectionToolbar(null)
          window.getSelection()?.removeAllRanges()
          return
        }
        closePlan()
        return
      }
      // Cmd/Ctrl+Shift+Enter = Execute with Praxis
      if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && e.shiftKey) {
        e.preventDefault()
        handleApprove('praxis')
      }
      // Cmd/Ctrl+Enter = Execute plan
      if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && !e.shiftKey) {
        e.preventDefault()
        handleApprove('code')
      }
      // Cmd/Ctrl+C when nothing selected = Copy plan
      if (e.key === 'c' && (e.metaKey || e.ctrlKey) && !window.getSelection()?.toString()) {
        e.preventDefault()
        handleCopy()
      }
      // Cmd/Ctrl+S = Download plan
      if (e.key === 's' && (e.metaKey || e.ctrlKey)) {
        e.preventDefault()
        handleDownload()
      }
    }
    window.addEventListener('keydown', handleKeyDown, { capture: true })
    onCleanup(() => window.removeEventListener('keydown', handleKeyDown, { capture: true }))
  })

  // Click outside to dismiss selection toolbar
  createEffect(() => {
    if (!isOpen()) return
    const handler = (e: MouseEvent) => handleDocumentClick(e)
    document.addEventListener('mousedown', handler)
    onCleanup(() => document.removeEventListener('mousedown', handler))
  })

  return (
    <Show when={isOpen() && activePlan()}>
      {(plan) => (
        <div
          class="flex flex-col overflow-hidden h-full w-full"
          style={{
            background: 'var(--bg)',
            animation: 'planOverlayIn 200ms ease-out',
          }}
        >
          {/* ── Header bar (48px) ── */}
          <header
            class="flex items-center gap-3 px-5 flex-shrink-0"
            style={{
              height: '48px',
              background: 'var(--surface-raised)',
              'border-bottom': '1px solid var(--border-subtle)',
            }}
          >
            {/* Left: Back + codename badge */}
            <button
              type="button"
              onClick={() => closePlan()}
              class="flex items-center gap-1.5 text-[13px] transition-opacity"
              style={{ color: 'var(--text-secondary)', opacity: '0.8' }}
            >
              <ArrowLeft class="w-4 h-4" />
              <span>Back to Chat</span>
            </button>

            <div class="flex items-center gap-2 ml-3">
              <div class="p-1 rounded" style={{ background: PLAN_ACCENT_SUBTLE }}>
                <ClipboardList class="w-4 h-4" style={{ color: PLAN_ACCENT }} />
              </div>
              <Show when={plan().codename}>
                <span class="text-[13px] font-bold tracking-wide" style={{ color: PLAN_ACCENT }}>
                  {plan().codename}
                </span>
              </Show>
            </div>

            <div class="flex-1" />

            {/* Right: Send Feedback + Approve + icons */}
            <div class="flex items-center gap-2">
              <button
                type="button"
                onClick={() => handleReject()}
                class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[12px] font-medium border transition-colors"
                style={{
                  color: PLAN_ACCENT,
                  'border-color': PLAN_ACCENT,
                  background: 'transparent',
                }}
                title="Send feedback from annotations"
              >
                Send Feedback
              </button>
              <button
                type="button"
                onClick={() => handleApprove('code')}
                class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[12px] font-medium transition-colors"
                style={{
                  color: '#fff',
                  background: '#22C55E',
                }}
                title="Approve plan (Ctrl+Enter)"
              >
                Approve
              </button>

              <div class="w-px h-5 mx-1" style={{ background: 'var(--border-subtle)' }} />

              <button
                type="button"
                onClick={handleCopy}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Copy as Markdown"
              >
                <Show when={copied()} fallback={<Copy class="w-4 h-4" />}>
                  <Check class="w-4 h-4" style={{ color: '#22C55E' }} />
                </Show>
              </button>
              <button
                type="button"
                onClick={handleDownload}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Download (Ctrl+S)"
              >
                <Download class="w-4 h-4" />
              </button>
              <button
                type="button"
                onClick={handleShare}
                class="p-2 rounded-md transition-colors"
                style={{ color: shareCopied() ? '#22C55E' : 'var(--text-muted)' }}
                title="Share link"
              >
                <Show when={shareCopied()} fallback={<Link2 class="w-4 h-4" />}>
                  <Check class="w-4 h-4" />
                </Show>
              </button>
              <button
                type="button"
                onClick={() => closePlan()}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Close (Esc)"
              >
                <X class="w-4 h-4" />
              </button>
            </div>
          </header>

          {/* ── 3-Panel body ── */}
          <div class="flex flex-1 overflow-hidden">
            {/* Left: TOC Sidebar */}
            <TOCSidebar
              steps={plan().steps}
              activeStepId={activeStepId()}
              collapsed={tocCollapsed()}
              planHistory={planHistory()}
              onScrollTo={scrollToStep}
              onToggleCollapse={() => setTocCollapsed((v) => !v)}
              onLoadPlan={loadPlanFromHistory}
            />

            {/* Center: Grid canvas + document card */}
            <main
              class="flex-1 overflow-y-auto plan-grid-bg"
              style={{
                background: 'var(--bg)',
                padding: '24px',
              }}
            >
              {/* Annotation toolstrip */}
              <AnnotationToolstrip activeMode={toolMode()} onModeChange={setToolMode} />

              {/* Document card */}
              <PlanDocument
                plan={plan()}
                annotations={annotations()}
                onMouseUp={handleDocumentMouseUp}
                onGlobalComment={() => setGlobalCommentOpen(true)}
                onCopyPlan={handleCopy}
                cardRef={() => {}}
              />
            </main>

            {/* Right: Annotations Panel */}
            <AnnotationsPanel
              annotations={annotations()}
              focusedId={focusedAnnotationId()}
              onFocus={(id) => setFocusedAnnotationId(id)}
              onRemove={(id) => removeAnnotation(id)}
            />
          </div>

          {/* ── Floating Selection Toolbar ── */}
          <Show when={selectionToolbar()}>
            {(toolbar) => (
              <div data-selection-toolbar>
                <SelectionToolbar
                  text={toolbar().text}
                  top={toolbar().top}
                  left={toolbar().left}
                  onCopy={() => {
                    navigator.clipboard.writeText(toolbar().text)
                    setSelectionToolbar(null)
                    window.getSelection()?.removeAllRanges()
                  }}
                  onDelete={() => {
                    addAnnotation({
                      id: generateId(),
                      type: 'deletion',
                      originalText: toolbar().text,
                      createdAt: Date.now(),
                    })
                    setSelectionToolbar(null)
                    window.getSelection()?.removeAllRanges()
                  }}
                  onComment={() => {
                    setCommentPopover({
                      text: toolbar().text,
                      top: toolbar().top + 56,
                      left: toolbar().left,
                    })
                    setSelectionToolbar(null)
                  }}
                  onQuickLabel={() => {
                    // Add as "clarify" quick label annotation
                    addAnnotation({
                      id: generateId(),
                      type: 'comment',
                      originalText: toolbar().text,
                      commentText: 'Needs clarification',
                      createdAt: Date.now(),
                    })
                    setSelectionToolbar(null)
                    window.getSelection()?.removeAllRanges()
                  }}
                  onClose={() => {
                    setSelectionToolbar(null)
                    window.getSelection()?.removeAllRanges()
                  }}
                />
              </div>
            )}
          </Show>

          {/* ── Comment Popover ── */}
          <Show when={commentPopover()}>
            {(popover) => (
              <div data-comment-popover>
                <CommentPopover
                  contextText={popover().text}
                  top={popover().top}
                  left={popover().left}
                  onSave={(comment) => {
                    addAnnotation({
                      id: generateId(),
                      type: 'comment',
                      originalText: popover().text,
                      commentText: comment,
                      createdAt: Date.now(),
                    })
                    setCommentPopover(null)
                    window.getSelection()?.removeAllRanges()
                  }}
                  onCancel={() => setCommentPopover(null)}
                />
              </div>
            )}
          </Show>

          {/* ── Global Comment Popover ── */}
          <Show when={globalCommentOpen()}>
            <div data-comment-popover>
              <CommentPopover
                contextText="Global comment on entire plan"
                top={200}
                left={window.innerWidth / 2}
                onSave={(comment) => {
                  addAnnotation({
                    id: generateId(),
                    type: 'global_comment',
                    originalText: 'Entire plan',
                    commentText: comment,
                    createdAt: Date.now(),
                  })
                  setGlobalCommentOpen(false)
                }}
                onCancel={() => setGlobalCommentOpen(false)}
              />
            </div>
          </Show>
        </div>
      )}
    </Show>
  )
}
