/**
 * Plan Full Screen — Thin wrapper bridging props-based API to store-based PlanOverlay architecture
 *
 * This component maintains backward compatibility with the props-based interface
 * (plan, onApprove, onRevise, onClose, sidebarTop, sidebarBottom, sidebarLabel)
 * while delegating to the decomposed PlanOverlay components internally.
 *
 * Milestone 4 decomposition approach:
 *   - Preserves external API for existing callers (MainArea.tsx)
 *   - Bridges props to the usePlanOverlay store
 *   - Reuses decomposed components: PlanHeader, TOCSidebar, AnnotationToolstrip,
 *     PlanDocument, AnnotationsPanel, SelectionToolbar, CommentPopover, QuickLabelPicker
 *   - Maintains focus/keyboard/Escape semantics via centralized keyboard handling
 *
 * Migration path: Callers should eventually migrate to usePlanOverlay() directly
 * and render PlanOverlay, but PlanFullScreen remains as a compatibility layer.
 */

import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  type JSX,
  onCleanup,
  Show,
} from 'solid-js'
import { apiFetch } from '../../../lib/api-client'
import { AnnotationsPanel } from './AnnotationsPanel'
import { AnnotationToolstrip } from './AnnotationToolstrip'
import { CommentPopover } from './CommentPopover'
import { formatPlanMarkdown, PlanDocument, parsePlanMarkdown } from './PlanDocument'
import { PlanHeader } from './PlanHeader'
import { QuickLabelPicker } from './QuickLabelPicker'
import { SelectionToolbar } from './SelectionToolbar'
import { TOCSidebar } from './TOCSidebar'
import {
  type EditorMode,
  generateId,
  type InputMethod,
  QUICK_LABELS,
  type SelectionInfo,
} from './types'

// Re-export types for backward compatibility
export type { PlanAnnotation } from '../../../stores/planOverlayStore'
export type { PlanData } from '../../../types/rust-ipc'

// ============================================================================
// Types
// ============================================================================

interface VersionEntry {
  id: string
  label: string
  description: string
  timeAgo: string
  isCurrent: boolean
}

// ============================================================================
// Props
// ============================================================================

export interface PlanFullScreenProps {
  plan: import('../../../types/rust-ipc').PlanData
  onApprove: (
    plan: import('../../../types/rust-ipc').PlanData,
    annotations: import('../../../stores/planOverlayStore').PlanAnnotation[]
  ) => void
  onRevise: (annotations: import('../../../stores/planOverlayStore').PlanAnnotation[]) => void
  onClose: () => void
  sidebarTop?: JSX.Element
  sidebarBottom?: JSX.Element
  sidebarLabel?: string
  // Diff state bridging from store (optional for backward compatibility)
  previousPlan?: import('../../../types/rust-ipc').PlanData | null
  showDiff?: boolean
  hasDiff?: boolean
  onToggleDiff?: () => void
}

// ============================================================================
// Sub-components (extracted for composition)
// ============================================================================

/** Version history panel — non-interactive placeholder (not yet implemented) */
const VersionHistoryPanel: Component<{
  versions: VersionEntry[]
  onSelect: (id: string) => void
  onClose: () => void
}> = () => {
  // This component is currently non-interactive - version history not implemented
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
        padding: '12px',
      }}
    >
      <span class="text-[11px] block text-center" style={{ color: 'var(--text-muted)' }}>
        Version history not available
      </span>
    </div>
  )
}

// ============================================================================
// Main Component — Thin wrapper orchestrating decomposed pieces
// ============================================================================

export const PlanFullScreen: Component<PlanFullScreenProps> = (props) => {
  // ─── Local state (bridge to props-based API) ─────────────────────────
  const [copied, setCopied] = createSignal(false)
  const [shareCopied, setShareCopied] = createSignal(false)
  const [tocCollapsed, setTocCollapsed] = createSignal(false)
  const [activeStepId, setActiveStepId] = createSignal<string | null>(null)
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [planHistory, _setPlanHistory] = createSignal<
    import('../../../types/rust-ipc').PlanSummary[]
  >([])
  const [focusedAnnotationId, setFocusedAnnotationId] = createSignal<string | null>(null)
  const [versionHistoryOpen, setVersionHistoryOpen] = createSignal(false)

  // Mode state
  const [editorMode, setEditorMode] = createSignal<EditorMode>('selection')
  const [inputMethod, setInputMethod] = createSignal<InputMethod>('drag')

  // Floating UI state
  const [selectionToolbar, setSelectionToolbar] = createSignal<SelectionInfo | null>(null)
  const [commentPopover, setCommentPopover] = createSignal<SelectionInfo | null>(null)
  const [quickLabelPicker, setQuickLabelPicker] = createSignal<SelectionInfo | null>(null)
  const [globalCommentOpen, setGlobalCommentOpen] = createSignal(false)

  // Annotation state (local to this component, passed back via onRevise)
  const [annotations, setAnnotations] = createSignal<
    import('../../../stores/planOverlayStore').PlanAnnotation[]
  >([])

  // ─── Effects ──────────────────────────────────────────────────────────

  // Reset all local state when the plan changes to prevent cross-plan leakage
  // Uses strong plan identity: requestId + step signatures ensure revised/reloaded
  // plans with same human-readable identifiers still clear annotations/floating state correctly
  createEffect(() => {
    // Build strong plan identity: requestId (if available) + full step content
    // This ensures revised plans with same codename/summary still clear annotations
    // Use spread + sort to avoid mutating original plan props
    const stepSignatures = props.plan.steps.map(
      (s) =>
        `${s.id}:${s.description}:${s.action}:${[...s.files].sort().join(',')}:${[...s.dependsOn].sort().join(',')}`
    )
    const planIdentity = JSON.stringify([
      props.plan.requestId ?? 'no-request-id',
      props.plan.codename ?? 'no-codename',
      props.plan.estimatedTurns,
      stepSignatures.join('|'),
    ])
    // eslint-disable-next-line no-console
    console.log('Plan identity changed, resetting state:', planIdentity.slice(0, 80))
    // Reset all per-plan local state
    setAnnotations([])
    setFocusedAnnotationId(null)
    setSelectionToolbar(null)
    setCommentPopover(null)
    setQuickLabelPicker(null)
    setGlobalCommentOpen(false)
    setVersionHistoryOpen(false)
    setActiveStepId(null)
    // Reset copy/share states
    setCopied(false)
    setShareCopied(false)
    // Note: Keep tocCollapsed and editorMode as they are UI preferences
  })

  // Focus management: set initial focus to the document for keyboard navigation
  let mainContentRef: HTMLElement | undefined
  createEffect(() => {
    // Small delay to ensure DOM is ready
    const timer = setTimeout(() => {
      if (mainContentRef) {
        mainContentRef.focus()
      }
    }, 100)
    onCleanup(() => clearTimeout(timer))
  })

  // Plan history loading disabled — not yet implemented
  // createEffect(() => {
  //   fetch('/api/plans')
  //     .then((r) => r.json())
  //     .then((plans: import('../../../types/rust-ipc').PlanSummary[]) => setPlanHistory(plans))
  //     .catch(() => {})
  // })

  // Keyboard shortcuts (preserved from original)
  createEffect(() => {
    const handleKeyDown = (e: KeyboardEvent): void => {
      // Check if an input element is focused - skip global shortcuts for text editing
      const activeElement = document.activeElement
      const isInputFocused =
        activeElement &&
        (activeElement.tagName === 'INPUT' ||
          activeElement.tagName === 'TEXTAREA' ||
          (activeElement as HTMLElement).isContentEditable)

      // Number keys 1-9, 0 for quick labels when picker is open
      if (quickLabelPicker() && /^[0-9]$/.test(e.key)) {
        e.preventDefault()
        const idx = e.key === '0' ? 9 : parseInt(e.key, 10) - 1
        const label = QUICK_LABELS[idx]
        if (label && quickLabelPicker()) {
          const info = quickLabelPicker()!
          addAnnotation({
            id: generateId(),
            type: 'comment',
            originalText: info.text,
            commentText: label.text,
            createdAt: Date.now(),
          })
          setQuickLabelPicker(null)
          window.getSelection()?.removeAllRanges()
        }
        return
      }

      if (e.key === 'Escape') {
        e.preventDefault()
        e.stopPropagation()
        // Escape layering: dismiss floating UI first, then close
        if (quickLabelPicker()) {
          setQuickLabelPicker(null)
          window.getSelection()?.removeAllRanges()
          return
        }
        if (commentPopover() || globalCommentOpen()) {
          setCommentPopover(null)
          setGlobalCommentOpen(false)
          return
        }
        if (selectionToolbar()) {
          setSelectionToolbar(null)
          window.getSelection()?.removeAllRanges()
          return
        }
        // Finally close the full screen
        props.onClose()
        return
      }

      // Skip global shortcuts when input is focused (e.g., comment textarea)
      if (isInputFocused) {
        return
      }

      // Cmd/Ctrl+Enter = Approve plan
      if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && !e.shiftKey) {
        e.preventDefault()
        handleApprove()
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

  // Click outside to dismiss floating UI
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
        setSelectionToolbar(null)
        setQuickLabelPicker(null)
      }
    }
    document.addEventListener('mousedown', handler)
    onCleanup(() => document.removeEventListener('mousedown', handler))
  })

  // ─── Helpers ─────────────────────────────────────────────────────────

  const addAnnotation = (ann: import('../../../stores/planOverlayStore').PlanAnnotation): void => {
    setAnnotations((prev) => [...prev, ann])
  }

  const removeAnnotation = (id: string): void => {
    setAnnotations((prev) => prev.filter((a) => a.id !== id))
  }

  const loadPlanFromHistory = (filename: string): void => {
    apiFetch(`/api/plans/${encodeURIComponent(filename)}`)
      .then((r) => {
        if (!r.ok) throw new Error('Not found')
        return r.text()
      })
      .then((content) => {
        const plan = parsePlanMarkdown(content)
        if (plan) {
          // In a real migration, this would update the parent or use the store
          // For now, this maintains the original behavior
          console.log('Loaded plan from history:', plan)
        }
      })
      .catch(() => {})
  }

  const handleApprove = (): void => {
    props.onApprove(props.plan, annotations())
  }

  const handleRevise = (): void => {
    props.onRevise(annotations())
  }

  const handleCopy = (): void => {
    const text = formatPlanMarkdown(props.plan)
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    })
  }

  const handleDownload = (): void => {
    const md = formatPlanMarkdown(props.plan)
    const blob = new Blob([md], { type: 'text/markdown' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `${props.plan.codename || 'plan'}.md`
    a.click()
    URL.revokeObjectURL(url)
  }

  const handleShare = (): void => {
    const json = JSON.stringify(props.plan)
    const encoded = btoa(unescape(encodeURIComponent(json)))
    const url = `${window.location.origin}${window.location.pathname}#plan=${encoded}`
    navigator.clipboard.writeText(url).then(() => {
      setShareCopied(true)
      setTimeout(() => setShareCopied(false), 2000)
    })
  }

  const scrollToStep = (stepId: string): void => {
    setActiveStepId(stepId)
    const el = document.getElementById(`plan-step-${stepId}`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    }
  }

  // ─── Mode decision tree (text selection handling) ─────────────────────

  const handleTextSelected = (text: string, rect: DOMRect): void => {
    // Clear all previous floating UI
    setSelectionToolbar(null)
    setCommentPopover(null)
    setQuickLabelPicker(null)

    switch (editorMode()) {
      case 'redline':
        // Instant deletion — no UI shown
        addAnnotation({
          id: generateId(),
          type: 'deletion',
          originalText: text,
          createdAt: Date.now(),
        })
        window.getSelection()?.removeAllRanges()
        break

      case 'comment':
        // Direct comment popover — no toolbar
        setCommentPopover({
          text,
          top: rect.bottom + 8,
          left: rect.left + rect.width / 2,
        })
        break

      case 'quickLabel':
        // Quick label picker — no toolbar
        setQuickLabelPicker({
          text,
          top: rect.bottom + 6,
          left: rect.right - 96,
        })
        break

      default:
        // 'selection' (Markup mode) — show floating toolbar
        setSelectionToolbar({
          text,
          top: rect.top - 48,
          left: rect.left + rect.width / 2,
        })
        break
    }
  }

  const handleDocumentMouseUp = (): void => {
    const sel = window.getSelection()
    if (!sel || sel.isCollapsed || !sel.toString().trim()) return
    const range = sel.getRangeAt(0)
    const rect = range.getBoundingClientRect()
    handleTextSelected(sel.toString(), rect)
  }

  // ─── Computed data ───────────────────────────────────────────────────

  const versions = createMemo((): VersionEntry[] => [
    {
      id: 'v3',
      label: 'v3 — Current',
      description: 'Revised from your comments',
      timeAgo: '2m ago',
      isCurrent: true,
    },
    {
      id: 'v2',
      label: 'v2 — User commented',
      description: '3 annotations added',
      timeAgo: '5m ago',
      isCurrent: false,
    },
    {
      id: 'v1',
      label: 'v1 — Initial plan',
      description: 'Generated by AVA',
      timeAgo: '8m ago',
      isCurrent: false,
    },
  ])

  // ─── Render ──────────────────────────────────────────────────────────

  return (
    <section
      aria-label={`Plan review: ${props.plan.codename ?? props.plan.summary}`}
      class="flex flex-col overflow-hidden h-full w-full"
      style={{
        background: 'var(--bg)',
        animation: 'planOverlayIn 200ms ease-out',
      }}
    >
      {/* Header — using decomposed PlanHeader */}
      <PlanHeader
        codename={props.plan.codename}
        copied={copied()}
        shareCopied={shareCopied()}
        hasDiff={props.hasDiff ?? false}
        showDiff={props.showDiff ?? false}
        onBack={() => props.onClose()}
        onApprove={handleApprove}
        onSendFeedback={handleRevise}
        onCopy={handleCopy}
        onDownload={handleDownload}
        onShare={handleShare}
        onClose={() => props.onClose()}
        onToggleDiff={props.onToggleDiff}
      />

      {/* 3-Panel body using decomposed components */}
      <div class="flex flex-1 overflow-hidden">
        <section aria-label="Table of contents">
          <TOCSidebar
            steps={props.plan.steps}
            activeStepId={activeStepId()}
            collapsed={tocCollapsed()}
            planHistory={planHistory()}
            onScrollTo={scrollToStep}
            onToggleCollapse={() => setTocCollapsed((prev) => !prev)}
            onLoadPlan={loadPlanFromHistory}
          />
        </section>

        {/* Center: Document area */}
        <main
          ref={(el) => {
            mainContentRef = el
          }}
          tabIndex={-1}
          aria-label="Plan document"
          class="flex-1 overflow-y-auto outline-none"
          style={{ background: 'var(--bg)' }}
        >
          {/* Annotation toolstrip — sticky at top */}
          <div
            class="sticky top-0 z-10"
            style={{
              background: 'var(--bg)',
              'border-bottom': '1px solid var(--border-subtle)',
            }}
          >
            <AnnotationToolstrip
              editorMode={editorMode()}
              inputMethod={inputMethod()}
              onEditorModeChange={setEditorMode}
              onInputMethodChange={setInputMethod}
            />
          </div>

          {/* Document card on grid canvas */}
          <PlanDocument
            plan={props.plan}
            annotations={annotations()}
            inputMethod={inputMethod()}
            showDiff={props.showDiff ?? false}
            previousPlan={props.previousPlan ?? null}
            onMouseUp={handleDocumentMouseUp}
            onTextSelected={handleTextSelected}
            onGlobalComment={() => setGlobalCommentOpen(true)}
            onCopyPlan={handleCopy}
            cardRef={() => {}}
          />
        </main>

        <section aria-label={props.sidebarLabel ?? 'Annotations'}>
          <AnnotationsPanel
            annotations={annotations()}
            focusedId={focusedAnnotationId()}
            onFocus={(id) => setFocusedAnnotationId(id)}
            onRemove={(id) => removeAnnotation(id)}
            sidebarTop={props.sidebarTop}
            sidebarBottom={props.sidebarBottom}
            sidebarLabel={props.sidebarLabel}
          />
        </section>
      </div>

      {/* Floating UI */}
      <Show when={selectionToolbar()}>
        {(toolbar) => (
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
              setQuickLabelPicker({
                text: toolbar().text,
                top: toolbar().top + 56,
                left: toolbar().left,
              })
              setSelectionToolbar(null)
            }}
            onClose={() => {
              setSelectionToolbar(null)
              window.getSelection()?.removeAllRanges()
            }}
          />
        )}
      </Show>

      {/* Comment Popover */}
      <Show when={commentPopover()}>
        {(popover) => (
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
        )}
      </Show>

      {/* Quick Label Picker */}
      <Show when={quickLabelPicker()}>
        {(picker) => (
          <QuickLabelPicker
            text={picker().text}
            top={picker().top}
            left={picker().left}
            onSelect={(_labelId, labelText) => {
              addAnnotation({
                id: generateId(),
                type: 'comment',
                originalText: picker().text,
                commentText: labelText,
                createdAt: Date.now(),
              })
              setQuickLabelPicker(null)
              window.getSelection()?.removeAllRanges()
            }}
            onCancel={() => setQuickLabelPicker(null)}
          />
        )}
      </Show>

      {/* Global Comment Popover */}
      <Show when={globalCommentOpen()}>
        <CommentPopover
          contextText="Global comment on entire plan"
          top={200}
          left={typeof window !== 'undefined' ? window.innerWidth / 2 : 500}
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
      </Show>

      {/* Version History Dropdown — disabled until proper history loading is implemented */}
      <Show when={versionHistoryOpen() && false}>
        <VersionHistoryPanel
          versions={versions()}
          onSelect={(id) => {
            console.log('Version history selection not yet implemented:', id)
            setVersionHistoryOpen(false)
          }}
          onClose={() => setVersionHistoryOpen(false)}
        />
      </Show>
    </section>
  )
}

export default PlanFullScreen
