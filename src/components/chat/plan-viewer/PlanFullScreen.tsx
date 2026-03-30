/**
 * Plan Full Screen (Plannotator-style)
 *
 * Three-column layout matching the Pencil design:
 *   1. TOC Sidebar (240px) -- collapsible table of contents with section tree
 *   2. Document Area (fill) -- toolbar + rich rendered plan content
 *   3. Annotations Sidebar (280px) -- annotation cards + empty state
 *
 * Floating overlays:
 *   - Markup Toolbar (on text selection in Markup mode)
 *   - Text Selection Popup (quick labels via zap button)
 *   - Global Comment Modal
 *   - Inline Comment Input
 *   - Version History Panel (dropdown from TOC header)
 *
 * Integrates into the layout store via viewingPlanId signal.
 * Opened from PlanCard's "Edit"/"View Full Plan" button.
 */

import {
  Ban,
  ChevronDown,
  ChevronRight,
  Copy,
  GitCompare,
  Image,
  Layers,
  Maximize2,
  MessageSquare,
  PanelLeftClose,
  Search,
  ShieldCheck,
  Sparkles,
  Strikethrough,
  TestTubes,
  Trash2,
  TriangleAlert,
  X,
  Zap,
} from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  type JSX,
  onCleanup,
  Show,
} from 'solid-js'
import type { PlanAnnotation } from '../../../stores/planOverlayStore'
import type { PlanData } from '../../../types/rust-ipc'
import { MarkdownContent } from '../MarkdownContent'
import { generateId } from './types'

// ============================================================================
// Types
// ============================================================================

type ToolbarMode = 'select' | 'markup'

interface TocSection {
  id: string
  label: string
  level: number
  children?: TocSection[]
  expanded?: boolean
}

interface VersionEntry {
  id: string
  label: string
  description: string
  timeAgo: string
  isCurrent: boolean
}

interface QuickLabelItem {
  id: string
  label: string
  color: string
  icon: Component<{ class?: string; style?: Record<string, string> | string }>
  dividerBefore?: boolean
}

const QUICK_LABELS: QuickLabelItem[] = [
  {
    id: 'clarify',
    label: 'Clarify this',
    color: '#8B5CF6',
    icon: (p) => (
      <span class={p.class} style={{ color: '#8B5CF6', 'font-weight': '700', 'font-size': '11px' }}>
        ?
      </span>
    ),
  },
  { id: 'verify', label: 'Verify this', color: '#3B82F6', icon: Search },
  { id: 'example', label: 'Give me an example', color: '#F59E0B', icon: TriangleAlert },
  { id: 'patterns', label: 'Match existing patterns', color: '#EC4899', icon: GitCompare },
  { id: 'alternatives', label: 'Consider alternatives', color: '#6366F1', icon: Layers },
  { id: 'regression', label: 'Ensure no regression', color: '#22C55E', icon: ShieldCheck },
  { id: 'out-of-scope', label: 'Out of scope', color: '#EF4444', icon: Ban, dividerBefore: true },
  { id: 'needs-tests', label: 'Needs tests', color: '#22C55E', icon: TestTubes },
]

// ============================================================================
// Props
// ============================================================================

export interface PlanFullScreenProps {
  plan: PlanData
  onApprove: (plan: PlanData, annotations: PlanAnnotation[]) => void
  onRevise: (annotations: PlanAnnotation[]) => void
  onClose: () => void
  sidebarTop?: JSX.Element
  sidebarBottom?: JSX.Element
  sidebarLabel?: string
}

// ============================================================================
// Sub-components
// ============================================================================

/** TOC sidebar item */
const TocItem: Component<{
  section: TocSection
  activeId: string | null
  onSelect: (id: string) => void
  onToggle: (id: string) => void
}> = (props) => {
  const isActive = (): boolean => props.activeId === props.section.id
  const hasChildren = (): boolean => (props.section.children?.length ?? 0) > 0
  const isExpanded = (): boolean => props.section.expanded ?? true

  return (
    <div>
      <button
        type="button"
        onClick={() => {
          if (hasChildren()) props.onToggle(props.section.id)
          props.onSelect(props.section.id)
        }}
        class="w-full text-left flex items-center gap-1.5 rounded transition-colors"
        style={{
          height: props.section.level === 0 ? '28px' : '26px',
          padding:
            props.section.level === 0 ? '0 10px' : `0 10px 0 ${10 + props.section.level * 18}px`,
          background: isActive() ? 'var(--accent)' : 'transparent',
          'border-radius': 'var(--radius-sm)',
        }}
      >
        <Show when={hasChildren()}>
          <Show
            when={isExpanded()}
            fallback={
              <ChevronRight
                class="w-3 h-3 flex-shrink-0"
                style={{ color: isActive() ? '#fff' : 'var(--text-muted)' }}
              />
            }
          >
            <ChevronDown
              class="w-3 h-3 flex-shrink-0"
              style={{ color: isActive() ? '#fff' : 'var(--text-muted)' }}
            />
          </Show>
        </Show>
        <span
          class="text-xs truncate"
          style={{
            color: isActive() ? '#fff' : 'var(--text-primary)',
            'font-weight': props.section.level === 0 ? '500' : 'normal',
          }}
        >
          {props.section.label}
        </span>
      </button>
      <Show when={hasChildren() && isExpanded()}>
        <For each={props.section.children}>
          {(child) => (
            <TocItem
              section={child}
              activeId={props.activeId}
              onSelect={props.onSelect}
              onToggle={props.onToggle}
            />
          )}
        </For>
      </Show>
    </div>
  )
}

/** Markup floating toolbar (5 icon buttons in a pill) */
const MarkupToolbar: Component<{
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
      data-selection-toolbar
      class="fixed z-[100] flex items-center rounded-[10px] border"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        background: 'var(--surface)',
        'border-color': 'var(--border-default)',
        'box-shadow': '0 16px 32px rgba(0,0,0,0.3)',
        padding: '0 6px',
        height: '38px',
        gap: '2px',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      <button
        type="button"
        onClick={() => props.onCopy()}
        class="flex items-center justify-center rounded-md transition-colors hover:bg-[var(--alpha-white-8)]"
        style={{ width: '32px', height: '28px', color: 'var(--text-muted)' }}
        title="Copy"
      >
        <Copy class="w-[15px] h-[15px]" />
      </button>
      <button
        type="button"
        onClick={() => props.onDelete()}
        class="flex items-center justify-center rounded-md transition-colors hover:bg-[var(--alpha-white-8)]"
        style={{ width: '32px', height: '28px', color: 'var(--error)' }}
        title="Mark for deletion"
      >
        <Trash2 class="w-[15px] h-[15px]" />
      </button>
      <button
        type="button"
        onClick={() => props.onComment()}
        class="flex items-center justify-center rounded-md transition-colors hover:bg-[var(--alpha-white-8)]"
        style={{ width: '32px', height: '28px', color: 'var(--text-muted)' }}
        title="Add comment"
      >
        <MessageSquare class="w-[15px] h-[15px]" />
      </button>
      <button
        type="button"
        onClick={() => props.onQuickLabel()}
        class="flex items-center justify-center rounded-md transition-colors"
        style={{
          width: '32px',
          height: '28px',
          color: 'var(--warning)',
          background: 'rgba(245, 166, 35, 0.14)',
        }}
        title="Quick label"
      >
        <Zap class="w-[15px] h-[15px]" />
      </button>
      <button
        type="button"
        onClick={() => props.onClose()}
        class="flex items-center justify-center rounded-md transition-colors hover:bg-[var(--alpha-white-8)]"
        style={{ width: '32px', height: '28px', color: 'var(--text-muted)' }}
        title="Dismiss"
      >
        <X class="w-[14px] h-[14px]" />
      </button>
    </div>
  )
}

/** Text Selection Popup (quick labels with color bars) */
const TextSelectionPopup: Component<{
  top: number
  left: number
  onSelect: (labelId: string, labelText: string) => void
  onClose: () => void
}> = (props) => {
  return (
    <div
      data-quick-label-picker
      class="fixed z-[110] rounded-[var(--radius-md)] border overflow-hidden"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        width: '240px',
        background: 'var(--surface)',
        'border-color': 'var(--border-default)',
        'box-shadow': '0 20px 40px rgba(0,0,0,0.3)',
        padding: '6px 4px',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      <For each={QUICK_LABELS}>
        {(label) => (
          <>
            <Show when={label.dividerBefore}>
              <div class="py-1 px-0">
                <div style={{ height: '1px', background: 'var(--border-default)' }} />
              </div>
            </Show>
            <button
              type="button"
              onClick={() => props.onSelect(label.id, label.label)}
              class="w-full text-left flex items-center gap-2 rounded-[var(--radius-sm)] transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ height: '30px', padding: '0 10px' }}
            >
              <div
                class="flex-shrink-0 rounded-sm"
                style={{ width: '3px', height: '16px', background: label.color }}
              />
              <label.icon class="w-3 h-3 flex-shrink-0" style={{ color: label.color }} />
              <span class="text-xs" style={{ color: 'var(--text-secondary)' }}>
                {label.label}
              </span>
            </button>
          </>
        )}
      </For>
    </div>
  )
}

/** Global Comment Modal */
const GlobalCommentModal: Component<{
  onAdd: (comment: string) => void
  onClose: () => void
}> = (props) => {
  const [text, setText] = createSignal('')

  return (
    // biome-ignore lint/a11y/noStaticElementInteractions: overlay backdrop click-to-close
    <div
      class="fixed inset-0 z-[120] flex items-start justify-center pt-[15vh]"
      onClick={(e) => {
        if (e.target === e.currentTarget) props.onClose()
      }}
      onKeyDown={() => {}}
    >
      <div
        class="rounded-[var(--radius-lg)] border overflow-hidden"
        style={{
          width: '380px',
          background: 'var(--surface)',
          'border-color': 'var(--border-default)',
          'box-shadow': '0 24px 48px rgba(0,0,0,0.35)',
          animation: 'selectionToolbarIn 150ms ease-out',
        }}
      >
        {/* Header */}
        <div class="flex items-center justify-between px-3.5" style={{ height: '40px' }}>
          <span class="text-[13px] font-semibold" style={{ color: 'var(--text-primary)' }}>
            Global Comment
          </span>
          <div class="flex items-center gap-1.5">
            <button
              type="button"
              class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="Expand"
            >
              <Maximize2 class="w-[13px] h-[13px]" />
            </button>
            <button
              type="button"
              onClick={() => props.onClose()}
              class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="Close"
            >
              <X class="w-[14px] h-[14px]" />
            </button>
          </div>
        </div>

        {/* Body */}
        <div
          class="px-3.5 pb-3.5"
          style={{ display: 'flex', 'flex-direction': 'column', gap: '10px' }}
        >
          <textarea
            value={text()}
            onInput={(e) => setText(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && text().trim()) {
                e.preventDefault()
                props.onAdd(text().trim())
              }
              if (e.key === 'Escape') props.onClose()
            }}
            ref={(el) => setTimeout(() => el.focus(), 50)}
            placeholder="Add a global comment..."
            class="w-full resize-none rounded-[var(--radius-md)] border px-3 py-3 text-[13px] outline-none"
            style={{
              height: '120px',
              color: 'var(--text-primary)',
              background: 'var(--surface-raised)',
              'border-color': 'var(--border-accent)',
            }}
          />
          <div class="flex items-center justify-between">
            <button
              type="button"
              class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="Attach image"
            >
              <Image class="w-4 h-4" />
            </button>
            <div class="flex items-center gap-2.5">
              <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                Ctrl+Enter
              </span>
              <button
                type="button"
                onClick={() => {
                  const val = text().trim()
                  if (val) props.onAdd(val)
                }}
                disabled={!text().trim()}
                class="rounded-[var(--radius-sm)] px-4 py-1.5 text-xs font-medium text-white transition-colors disabled:opacity-40"
                style={{ background: 'var(--accent)' }}
              >
                Add
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

/** Inline Comment Input (floating near selected text) */
const InlineCommentInput: Component<{
  quotedText: string
  top: number
  left: number
  onSave: (comment: string) => void
  onClose: () => void
}> = (props) => {
  const [text, setText] = createSignal('')

  return (
    <div
      data-comment-popover
      class="fixed z-[110] rounded-[var(--radius-lg)] border overflow-hidden"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        width: '380px',
        background: 'var(--surface)',
        'border-color': 'var(--border-default)',
        'box-shadow': '0 24px 48px rgba(0,0,0,0.35)',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      {/* Header with quoted text */}
      <div class="flex items-center justify-between px-3.5" style={{ height: '36px' }}>
        <span class="text-xs truncate" style={{ color: 'var(--text-secondary)' }}>
          &ldquo;{props.quotedText.slice(0, 40)}
          {props.quotedText.length > 40 ? '...' : ''}&rdquo;
        </span>
        <div class="flex items-center gap-1.5 flex-shrink-0">
          <button
            type="button"
            class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
            style={{ color: 'var(--text-muted)' }}
          >
            <Maximize2 class="w-[13px] h-[13px]" />
          </button>
          <button
            type="button"
            onClick={() => props.onClose()}
            class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
            style={{ color: 'var(--text-muted)' }}
          >
            <X class="w-[14px] h-[14px]" />
          </button>
        </div>
      </div>

      {/* Body */}
      <div
        class="px-3.5 pb-3.5"
        style={{ display: 'flex', 'flex-direction': 'column', gap: '10px' }}
      >
        <textarea
          value={text()}
          onInput={(e) => setText(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && text().trim()) {
              e.preventDefault()
              props.onSave(text().trim())
            }
            if (e.key === 'Escape') props.onClose()
          }}
          ref={(el) => setTimeout(() => el.focus(), 50)}
          placeholder="Add a comment..."
          class="w-full resize-none rounded-[var(--radius-md)] border px-3 py-3 text-[13px] outline-none"
          style={{
            height: '100px',
            color: 'var(--text-primary)',
            background: 'var(--surface-raised)',
            'border-color': 'var(--border-accent)',
          }}
        />
        <div class="flex items-center justify-between">
          <button
            type="button"
            class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
            style={{ color: 'var(--text-muted)' }}
            title="Attach image"
          >
            <Image class="w-4 h-4" />
          </button>
          <div class="flex items-center gap-2.5">
            <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
              Ctrl+Enter
            </span>
            <button
              type="button"
              onClick={() => {
                const val = text().trim()
                if (val) props.onSave(val)
              }}
              disabled={!text().trim()}
              class="rounded-[var(--radius-sm)] px-4 py-1.5 text-xs font-medium text-white transition-colors disabled:opacity-40"
              style={{ background: 'var(--system-purple, #8B5CF6)' }}
            >
              Save
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

/** Version History Panel (dropdown) */
const VersionHistoryPanel: Component<{
  versions: VersionEntry[]
  onSelect: (id: string) => void
  onClose: () => void
}> = (props) => {
  return (
    <div
      class="absolute z-[90] rounded-[var(--radius-md)] border overflow-hidden"
      style={{
        top: '44px',
        left: '8px',
        width: '280px',
        background: 'var(--surface)',
        'border-color': 'var(--border-default)',
        'box-shadow': '0 20px 40px rgba(0,0,0,0.3)',
        padding: '6px 4px',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      <div class="px-2.5 py-1.5">
        <span
          class="text-[9px] font-semibold tracking-widest uppercase"
          style={{ color: 'var(--text-muted)', 'letter-spacing': '1px' }}
        >
          VERSIONS
        </span>
      </div>
      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
        <For each={props.versions}>
          {(version) => (
            <button
              type="button"
              onClick={() => {
                props.onSelect(version.id)
                props.onClose()
              }}
              class="w-full text-left flex items-center gap-2.5 rounded-[var(--radius-sm)] transition-colors"
              style={{
                height: '36px',
                padding: '0 10px',
                background: version.isCurrent ? 'var(--accent)' : 'transparent',
              }}
            >
              <div
                class="flex-shrink-0 rounded-full"
                style={{
                  width: '8px',
                  height: '8px',
                  background: version.isCurrent ? '#fff' : 'var(--text-muted)',
                }}
              />
              <div
                class="flex-1 min-w-0"
                style={{ display: 'flex', 'flex-direction': 'column', gap: '1px' }}
              >
                <span
                  class="text-[11px] font-medium truncate"
                  style={{ color: version.isCurrent ? '#fff' : 'var(--text-secondary)' }}
                >
                  {version.label}
                </span>
                <span
                  class="text-[9px] truncate"
                  style={{
                    color: version.isCurrent ? 'rgba(255,255,255,0.7)' : 'var(--text-muted)',
                  }}
                >
                  {version.description} &middot; {version.timeAgo}
                </span>
              </div>
            </button>
          )}
        </For>
      </div>
    </div>
  )
}

// ============================================================================
// Annotation Card (for sidebar)
// ============================================================================

const AnnotationCard: Component<{
  annotation: PlanAnnotation
  focused: boolean
  onFocus: () => void
  onRemove: () => void
}> = (props) => {
  const borderColor = (): string => {
    switch (props.annotation.type) {
      case 'deletion':
        return 'var(--error)'
      case 'comment':
        return '#EAB308'
      default:
        return 'var(--accent)'
    }
  }

  const typeLabel = (): string => {
    switch (props.annotation.type) {
      case 'deletion':
        return 'Deletion'
      case 'comment':
        return 'Comment'
      default:
        return 'Global'
    }
  }

  const typeBg = (): string => {
    switch (props.annotation.type) {
      case 'deletion':
        return 'rgba(239, 68, 68, 0.12)'
      case 'comment':
        return 'rgba(234, 179, 8, 0.12)'
      default:
        return 'rgba(139, 92, 246, 0.12)'
    }
  }

  const typeColor = (): string => {
    switch (props.annotation.type) {
      case 'deletion':
        return '#EF4444'
      case 'comment':
        return '#EAB308'
      default:
        return '#8B5CF6'
    }
  }

  return (
    <article
      onClick={() => props.onFocus()}
      onKeyDown={(e) => {
        if (e.key === 'Enter') props.onFocus()
      }}
      class="w-full cursor-pointer rounded-lg border-l-[3px] border p-3 transition-[background-color]"
      style={{
        'border-left-color': borderColor(),
        'border-color': props.focused ? 'rgba(59,130,246,0.3)' : 'var(--border-subtle)',
        background: props.focused ? 'rgba(59,130,246,0.08)' : 'var(--alpha-white-3)',
      }}
    >
      <div class="flex items-center justify-between mb-1.5">
        <div class="flex items-center gap-1.5">
          <Show when={props.annotation.type === 'comment'}>
            <MessageSquare class="w-3 h-3" style={{ color: typeColor() }} />
          </Show>
          <Show when={props.annotation.type === 'deletion'}>
            <Trash2 class="w-3 h-3" style={{ color: typeColor() }} />
          </Show>
          <span
            class="inline-flex items-center px-1.5 py-0.5 rounded text-[9px] font-semibold uppercase tracking-wider"
            style={{ background: typeBg(), color: typeColor() }}
          >
            {typeLabel()}
          </span>
        </div>
        <button
          type="button"
          onClick={(e) => {
            e.stopPropagation()
            props.onRemove()
          }}
          class="p-0.5 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
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
          'text-decoration': props.annotation.type === 'deletion' ? 'line-through' : 'none',
          'text-decoration-color': props.annotation.type === 'deletion' ? '#EF4444' : undefined,
        }}
      >
        &ldquo;{props.annotation.originalText.slice(0, 80)}
        {props.annotation.originalText.length > 80 ? '...' : ''}&rdquo;
      </p>
      <Show when={props.annotation.commentText}>
        <p class="text-[11px] italic" style={{ color: 'var(--text-muted)' }}>
          {props.annotation.commentText}
        </p>
      </Show>
    </article>
  )
}

// ============================================================================
// Main Component
// ============================================================================

export const PlanFullScreen: Component<PlanFullScreenProps> = (props) => {
  // ─── State ──────────────────────────────────────────────────────────
  const [toolbarMode, setToolbarMode] = createSignal<ToolbarMode>('select')
  const [tocCollapsed, setTocCollapsed] = createSignal(false)
  const [activeSection, setActiveSection] = createSignal<string>('overview')
  const [expandedSections, setExpandedSections] = createSignal<Set<string>>(
    new Set(['phase-1', 'phase-2'])
  )
  const [annotations, setAnnotations] = createSignal<PlanAnnotation[]>([])
  const [focusedAnnotationId, setFocusedAnnotationId] = createSignal<string | null>(null)
  const [versionHistoryOpen, setVersionHistoryOpen] = createSignal(false)
  const [globalCommentOpen, setGlobalCommentOpen] = createSignal(false)

  // Floating toolbar state
  const [markupToolbar, setMarkupToolbar] = createSignal<{
    top: number
    left: number
    text: string
  } | null>(null)
  const [quickLabelPopup, setQuickLabelPopup] = createSignal<{ top: number; left: number } | null>(
    null
  )
  const [inlineComment, setInlineComment] = createSignal<{
    top: number
    left: number
    text: string
  } | null>(null)

  // ─── TOC generation ─────────────────────────────────────────────────

  /** Build TOC sections from plan data */
  const tocSections = createMemo((): TocSection[] => {
    const plan = props.plan
    const sections: TocSection[] = [{ id: 'overview', label: 'Overview', level: 0 }]

    // Group steps by action into "phases"
    let currentPhase: TocSection | null = null
    let phaseIndex = 0

    for (const step of plan.steps) {
      // Every 2-3 steps or on action change, start a new phase
      if (!currentPhase || (currentPhase.children && currentPhase.children.length >= 3)) {
        phaseIndex++
        const phaseId = `phase-${phaseIndex}`
        currentPhase = {
          id: phaseId,
          label: `Phase ${phaseIndex}: ${step.action.charAt(0).toUpperCase() + step.action.slice(1)}`,
          level: 0,
          children: [],
          expanded: expandedSections().has(phaseId),
        }
        sections.push(currentPhase)
      }

      currentPhase.children!.push({
        id: `step-${step.id}`,
        label: step.description,
        level: 1,
      })
    }

    return sections
  })

  // ─── Version history (mock data, would come from plan history) ──────

  const versions = createMemo((): VersionEntry[] => [
    {
      id: 'v3',
      label: 'v3 \u2014 Current',
      description: 'Revised from your comments',
      timeAgo: '2m ago',
      isCurrent: true,
    },
    {
      id: 'v2',
      label: 'v2 \u2014 User commented',
      description: '3 annotations added',
      timeAgo: '5m ago',
      isCurrent: false,
    },
    {
      id: 'v1',
      label: 'v1 \u2014 Initial plan',
      description: 'Generated by AVA',
      timeAgo: '8m ago',
      isCurrent: false,
    },
  ])

  // ─── Markdown content for the document ──────────────────────────────

  const planMarkdown = createMemo((): string => {
    const plan = props.plan
    const parts: string[] = []

    parts.push(`# Implementation Plan: ${plan.summary}`)
    parts.push('')
    parts.push('## Overview')
    parts.push('')
    parts.push(`${plan.codename ? `**${plan.codename}** \u2014 ` : ''}${plan.summary}`)
    parts.push('')

    let phaseIndex = 0
    for (let i = 0; i < plan.steps.length; i++) {
      const step = plan.steps[i]
      if (i % 3 === 0) {
        phaseIndex++
        parts.push(
          `## Phase ${phaseIndex}: ${step.action.charAt(0).toUpperCase() + step.action.slice(1)}`
        )
        parts.push('')
      }

      parts.push(`### ${step.description}`)
      parts.push('')

      if (step.files.length > 0) {
        for (const file of step.files) {
          parts.push(`- \`${file}\``)
        }
        parts.push('')
      }

      if (step.dependsOn.length > 0) {
        const depLabels = step.dependsOn.map((depId) => {
          const idx = plan.steps.findIndex((s) => s.id === depId)
          return idx >= 0 ? `Step ${idx + 1}` : depId
        })
        parts.push(`> Depends on: ${depLabels.join(', ')}`)
        parts.push('')
      }
    }

    return parts.join('\n')
  })

  // ─── Annotation helpers ─────────────────────────────────────────────

  const addAnnotation = (ann: PlanAnnotation): void => {
    setAnnotations((prev) => [...prev, ann])
  }

  const removeAnnotation = (id: string): void => {
    setAnnotations((prev) => prev.filter((a) => a.id !== id))
  }

  // ─── Text selection handling ────────────────────────────────────────

  const handleDocMouseUp = (): void => {
    if (toolbarMode() !== 'markup') return

    const sel = window.getSelection()
    if (!sel || sel.isCollapsed || !sel.toString().trim()) return

    const range = sel.getRangeAt(0)
    const rect = range.getBoundingClientRect()
    const text = sel.toString().trim()

    setMarkupToolbar({
      text,
      top: rect.top - 48,
      left: rect.left + rect.width / 2,
    })
  }

  // Dismiss floating UI on outside click
  createEffect(() => {
    const handler = (e: MouseEvent): void => {
      const target = e.target as HTMLElement
      if (
        target.closest('[data-selection-toolbar]') ||
        target.closest('[data-comment-popover]') ||
        target.closest('[data-quick-label-picker]')
      ) {
        return
      }
      const sel = window.getSelection()
      if (!sel || sel.isCollapsed) {
        setMarkupToolbar(null)
        setQuickLabelPopup(null)
      }
    }
    document.addEventListener('mousedown', handler)
    onCleanup(() => document.removeEventListener('mousedown', handler))
  })

  // ─── Keyboard shortcuts ─────────────────────────────────────────────

  createEffect(() => {
    const handleKeyDown = (e: KeyboardEvent): void => {
      if (e.key === 'Escape') {
        e.preventDefault()
        if (quickLabelPopup()) {
          setQuickLabelPopup(null)
          return
        }
        if (inlineComment()) {
          setInlineComment(null)
          return
        }
        if (globalCommentOpen()) {
          setGlobalCommentOpen(false)
          return
        }
        if (markupToolbar()) {
          setMarkupToolbar(null)
          window.getSelection()?.removeAllRanges()
          return
        }
        props.onClose()
      }
    }
    window.addEventListener('keydown', handleKeyDown, { capture: true })
    onCleanup(() => window.removeEventListener('keydown', handleKeyDown, { capture: true }))
  })

  // ─── Section toggle ─────────────────────────────────────────────────

  const toggleSection = (id: string): void => {
    setExpandedSections((prev) => {
      const next = new Set(prev)
      if (next.has(id)) next.delete(id)
      else next.add(id)
      return next
    })
  }

  const scrollToSection = (id: string): void => {
    setActiveSection(id)
    // Try to scroll the heading into view
    const el = document.getElementById(`plan-section-${id}`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'start' })
    }
  }

  // ─── Render ─────────────────────────────────────────────────────────

  return (
    <div
      class="flex h-full w-full overflow-hidden"
      style={{
        background: 'var(--background-subtle, var(--surface))',
        animation: 'planOverlayIn 200ms ease-out',
      }}
    >
      {/* ══════════════════════════════════════════════════════════════ */}
      {/* 1. TOC Sidebar (240px)                                       */}
      {/* ══════════════════════════════════════════════════════════════ */}
      <Show when={!tocCollapsed()}>
        <aside
          class="flex flex-col h-full flex-shrink-0 overflow-hidden relative"
          style={{
            width: '240px',
            background: 'var(--surface)',
          }}
        >
          {/* Header */}
          <div
            class="flex items-center justify-between px-3.5 flex-shrink-0"
            style={{ height: '44px' }}
          >
            <span
              class="text-[9px] font-semibold tracking-widest uppercase"
              style={{ color: 'var(--text-muted)', 'letter-spacing': '1px' }}
            >
              CONTENTS
            </span>
            <button
              type="button"
              onClick={() => setTocCollapsed(true)}
              class="p-1 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="Collapse sidebar"
            >
              <PanelLeftClose class="w-3.5 h-3.5" />
            </button>
          </div>

          {/* Section tree */}
          <nav
            class="flex-1 overflow-y-auto px-2 pb-3"
            style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}
          >
            <For each={tocSections()}>
              {(section) => (
                <TocItem
                  section={section}
                  activeId={activeSection()}
                  onSelect={scrollToSection}
                  onToggle={toggleSection}
                />
              )}
            </For>
          </nav>

          {/* Version History Dropdown */}
          <Show when={versionHistoryOpen()}>
            <VersionHistoryPanel
              versions={versions()}
              onSelect={() => {}}
              onClose={() => setVersionHistoryOpen(false)}
            />
          </Show>
        </aside>

        {/* Divider */}
        <div style={{ width: '1px', background: 'var(--border-subtle)', 'flex-shrink': '0' }} />
      </Show>

      {/* ══════════════════════════════════════════════════════════════ */}
      {/* 2. Document Area (fill)                                      */}
      {/* ══════════════════════════════════════════════════════════════ */}
      <div class="flex-1 flex flex-col min-w-0 overflow-hidden">
        {/* ─── Toolbar (44px) ────────────────────────────────────── */}
        <div
          class="flex items-center justify-between px-4 flex-shrink-0"
          style={{
            height: '44px',
            background: 'var(--surface)',
          }}
        >
          {/* Left side */}
          <div class="flex items-center gap-1.5" style={{ height: '100%' }}>
            {/* Show sidebar toggle when collapsed */}
            <Show when={tocCollapsed()}>
              <button
                type="button"
                onClick={() => setTocCollapsed(false)}
                class="p-1.5 rounded transition-colors hover:bg-[var(--alpha-white-5)] mr-1"
                style={{ color: 'var(--text-muted)' }}
                title="Show sidebar"
              >
                <PanelLeftClose class="w-3.5 h-3.5" style={{ transform: 'scaleX(-1)' }} />
              </button>
            </Show>

            {/* Select / Markup mode pills */}
            <button
              type="button"
              onClick={() => setToolbarMode('select')}
              class="flex items-center gap-1.5 rounded-[20px] px-3 py-[5px] text-xs transition-colors"
              style={{
                color: toolbarMode() === 'select' ? 'var(--text-primary)' : 'var(--text-muted)',
                background: toolbarMode() === 'select' ? 'rgba(255,255,255,0.03)' : 'transparent',
                border:
                  toolbarMode() === 'select'
                    ? '1px solid var(--border-default)'
                    : '1px solid transparent',
              }}
            >
              Select
            </button>
            <button
              type="button"
              onClick={() => setToolbarMode('markup')}
              class="flex items-center gap-1.5 rounded-[20px] px-3 py-[5px] text-xs transition-colors"
              style={{
                color: toolbarMode() === 'markup' ? 'var(--text-primary)' : 'var(--text-muted)',
                background: toolbarMode() === 'markup' ? 'rgba(255,255,255,0.03)' : 'transparent',
                border:
                  toolbarMode() === 'markup'
                    ? '1px solid var(--border-default)'
                    : '1px solid transparent',
              }}
            >
              Markup
            </button>

            {/* Divider */}
            <div
              style={{
                width: '1px',
                height: '20px',
                background: 'var(--border-default)',
                margin: '0 4px',
              }}
            />

            {/* Tool icons */}
            <button
              type="button"
              onClick={() => setGlobalCommentOpen(true)}
              class="p-1.5 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="Add comment"
            >
              <MessageSquare class="w-[15px] h-[15px]" />
            </button>
            <button
              type="button"
              class="p-1.5 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="Strikethrough"
            >
              <Strikethrough class="w-[15px] h-[15px]" />
            </button>
            <button
              type="button"
              class="p-1.5 rounded transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{ color: 'var(--text-muted)' }}
              title="AI suggestions"
            >
              <Sparkles class="w-[15px] h-[15px]" />
            </button>

            {/* Divider */}
            <div
              style={{
                width: '1px',
                height: '20px',
                background: 'var(--border-default)',
                margin: '0 4px',
              }}
            />

            {/* Ask input */}
            <div
              class="flex items-center gap-1.5 rounded-[var(--radius-sm)] border px-2.5"
              style={{
                height: '28px',
                width: '180px',
                background: 'var(--surface-raised)',
                'border-color': 'var(--border-default)',
              }}
            >
              <Sparkles class="w-3 h-3 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
              <input
                type="text"
                placeholder="how does this work?"
                class="flex-1 min-w-0 bg-transparent text-xs outline-none"
                style={{ color: 'var(--text-primary)' }}
              />
            </div>
          </div>

          {/* Right side */}
          <div class="flex items-center gap-3" style={{ height: '100%' }}>
            {/* Global comment link */}
            <button
              type="button"
              onClick={() => setGlobalCommentOpen(true)}
              class="flex items-center gap-1.5 text-xs transition-colors hover:underline"
              style={{ color: 'var(--text-muted)' }}
            >
              <MessageSquare class="w-3.5 h-3.5" />
              Global comment
            </button>

            {/* Copy plan link */}
            <button
              type="button"
              onClick={() => {
                navigator.clipboard.writeText(planMarkdown())
              }}
              class="flex items-center gap-1.5 text-xs transition-colors hover:underline"
              style={{ color: 'var(--text-muted)' }}
            >
              <Copy class="w-3.5 h-3.5" />
              Copy plan
            </button>

            {/* Divider */}
            <div style={{ width: '1px', height: '20px', background: 'var(--border-default)' }} />

            {/* Send for Revisions */}
            <button
              type="button"
              onClick={() => props.onRevise(annotations())}
              class="flex items-center gap-1.5 rounded-[var(--radius-sm)] border px-3.5 py-[5px] text-xs font-medium transition-colors hover:bg-[var(--alpha-white-5)]"
              style={{
                color: 'var(--text-secondary)',
                background: 'rgba(255,255,255,0.024)',
                'border-color': 'var(--border-default)',
              }}
            >
              Send for Revisions
            </button>

            {/* Submit Plan */}
            <button
              type="button"
              onClick={() => props.onApprove(props.plan, annotations())}
              class="flex items-center gap-1.5 rounded-[var(--radius-sm)] px-4 py-[5px] text-xs font-medium text-white transition-colors"
              style={{ background: 'var(--accent)' }}
            >
              Submit Plan
            </button>
          </div>
        </div>

        {/* Toolbar divider */}
        <div style={{ height: '1px', background: 'var(--border-subtle)', 'flex-shrink': '0' }} />

        {/* ─── Document scroll area ──────────────────────────────── */}
        {/* biome-ignore lint/a11y/noStaticElementInteractions: text selection handler */}
        <div class="flex-1 overflow-y-auto" onMouseUp={handleDocMouseUp}>
          <div
            class="plan-fullscreen-document select-text"
            style={{
              padding: '32px 60px',
              'max-width': '900px',
            }}
          >
            <MarkdownContent content={planMarkdown()} messageRole="assistant" isStreaming={false} />
          </div>
        </div>
      </div>

      {/* Divider before annotations */}
      <div style={{ width: '1px', background: 'var(--border-subtle)', 'flex-shrink': '0' }} />

      {/* ══════════════════════════════════════════════════════════════ */}
      {/* 3. Annotations Sidebar (280px)                               */}
      {/* ══════════════════════════════════════════════════════════════ */}
      <aside
        class="flex flex-col h-full flex-shrink-0 overflow-hidden"
        style={{
          width: '280px',
          background: 'var(--surface)',
        }}
      >
        {/* Header */}
        <div
          class="flex items-center justify-between px-4 flex-shrink-0"
          style={{ height: '44px' }}
        >
          <div class="flex items-center gap-2">
            <span
              class="text-[9px] font-semibold tracking-widest uppercase"
              style={{ color: 'var(--text-muted)', 'letter-spacing': '1px' }}
            >
              {props.sidebarLabel ?? 'ANNOTATIONS'}
            </span>
            <Show when={annotations().length > 0}>
              <span
                class="inline-flex items-center justify-center min-w-[18px] h-[18px] rounded-full text-[10px] font-bold px-1"
                style={{
                  background: 'rgba(139, 92, 246, 0.12)',
                  color: '#8B5CF6',
                }}
              >
                {annotations().length}
              </span>
            </Show>
          </div>
        </div>

        {/* Cards */}
        <div
          class="flex-1 overflow-y-auto px-3 pb-3"
          style={{ display: 'flex', 'flex-direction': 'column', gap: '8px' }}
        >
          <Show when={props.sidebarTop}>{props.sidebarTop}</Show>
          <Show
            when={annotations().length > 0}
            fallback={
              <div class="flex flex-col items-center justify-center h-full gap-3 text-center px-4">
                <div
                  class="w-10 h-10 rounded-full flex items-center justify-center"
                  style={{ background: 'var(--alpha-white-5)' }}
                >
                  <MessageSquare class="w-5 h-5" style={{ color: 'var(--text-muted)' }} />
                </div>
                <span class="text-xs" style={{ color: 'var(--text-muted)' }}>
                  Select text to add annotations
                </span>
              </div>
            }
          >
            <For each={annotations()}>
              {(ann) => (
                <AnnotationCard
                  annotation={ann}
                  focused={focusedAnnotationId() === ann.id}
                  onFocus={() => setFocusedAnnotationId(ann.id)}
                  onRemove={() => removeAnnotation(ann.id)}
                />
              )}
            </For>
          </Show>
          <Show when={props.sidebarBottom}>{props.sidebarBottom}</Show>
        </div>
      </aside>

      {/* ══════════════════════════════════════════════════════════════ */}
      {/* Floating overlays                                            */}
      {/* ══════════════════════════════════════════════════════════════ */}

      {/* Markup Toolbar */}
      <Show when={markupToolbar()}>
        {(toolbar) => (
          <MarkupToolbar
            top={toolbar().top}
            left={toolbar().left}
            onCopy={() => {
              navigator.clipboard.writeText(toolbar().text)
              setMarkupToolbar(null)
              window.getSelection()?.removeAllRanges()
            }}
            onDelete={() => {
              addAnnotation({
                id: generateId(),
                type: 'deletion',
                originalText: toolbar().text,
                createdAt: Date.now(),
              })
              setMarkupToolbar(null)
              window.getSelection()?.removeAllRanges()
            }}
            onComment={() => {
              setInlineComment({
                text: toolbar().text,
                top: toolbar().top + 56,
                left: toolbar().left,
              })
              setMarkupToolbar(null)
            }}
            onQuickLabel={() => {
              setQuickLabelPopup({
                top: toolbar().top + 56,
                left: toolbar().left,
              })
              setMarkupToolbar(null)
            }}
            onClose={() => {
              setMarkupToolbar(null)
              window.getSelection()?.removeAllRanges()
            }}
          />
        )}
      </Show>

      {/* Quick Label / Text Selection Popup */}
      <Show when={quickLabelPopup()}>
        {(popup) => (
          <TextSelectionPopup
            top={popup().top}
            left={popup().left}
            onSelect={(_labelId, labelText) => {
              const toolbar = markupToolbar()
              const comment = inlineComment()
              const contextText = toolbar?.text || comment?.text || 'Selected text'
              addAnnotation({
                id: generateId(),
                type: 'comment',
                originalText: contextText,
                commentText: labelText,
                createdAt: Date.now(),
              })
              setQuickLabelPopup(null)
              window.getSelection()?.removeAllRanges()
            }}
            onClose={() => setQuickLabelPopup(null)}
          />
        )}
      </Show>

      {/* Inline Comment Input */}
      <Show when={inlineComment()}>
        {(ic) => (
          <InlineCommentInput
            quotedText={ic().text}
            top={ic().top}
            left={ic().left}
            onSave={(comment) => {
              addAnnotation({
                id: generateId(),
                type: 'comment',
                originalText: ic().text,
                commentText: comment,
                createdAt: Date.now(),
              })
              setInlineComment(null)
              window.getSelection()?.removeAllRanges()
            }}
            onClose={() => setInlineComment(null)}
          />
        )}
      </Show>

      {/* Global Comment Modal */}
      <Show when={globalCommentOpen()}>
        <GlobalCommentModal
          onAdd={(comment) => {
            addAnnotation({
              id: generateId(),
              type: 'global_comment',
              originalText: 'Entire plan',
              commentText: comment,
              createdAt: Date.now(),
            })
            setGlobalCommentOpen(false)
          }}
          onClose={() => setGlobalCommentOpen(false)}
        />
      </Show>
    </div>
  )
}

export default PlanFullScreen
