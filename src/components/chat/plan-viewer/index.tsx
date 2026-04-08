/**
 * Plan Viewer — Plannotator-style full-screen plan overlay
 *
 * Orchestrates all sub-components: header, TOC sidebar, annotation toolstrip,
 * document card, selection toolbar, comment popover, quick label picker,
 * and annotations panel.
 *
 * Mode decision tree (matches Plannotator interaction model):
 *   redline    -> instant DELETION annotation (no UI)
 *   comment    -> CommentPopover opens directly (no toolbar)
 *   quickLabel -> QuickLabelPicker opens directly
 *   selection  -> floating SelectionToolbar with Copy/Delete/Comment/Label/Close
 */

import { type Component, createEffect, createSignal, onCleanup, Show } from 'solid-js'
import { useAgent } from '../../../hooks/useAgent'
import { usePlanOverlay } from '../../../stores/planOverlayStore'
import type { PlanSummary } from '../../../types/rust-ipc'
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
    previousPlan,
    showDiff,
    toggleDiff,
    hasDiff,
  } = usePlanOverlay()
  const agent = useAgent()

  // UI signals
  const [copied, setCopied] = createSignal(false)
  const [shareCopied, setShareCopied] = createSignal(false)
  const [tocCollapsed, setTocCollapsed] = createSignal(false)
  const [activeStepId, setActiveStepId] = createSignal<string | null>(null)
  const [planHistory, setPlanHistory] = createSignal<PlanSummary[]>([])
  const [focusedAnnotationId, setFocusedAnnotationId] = createSignal<string | null>(null)

  // Mode state
  const [editorMode, setEditorMode] = createSignal<EditorMode>('selection')
  const [inputMethod, setInputMethod] = createSignal<InputMethod>('drag')

  // Floating UI state
  const [selectionToolbar, setSelectionToolbar] = createSignal<SelectionInfo | null>(null)
  const [commentPopover, setCommentPopover] = createSignal<SelectionInfo | null>(null)
  const [quickLabelPicker, setQuickLabelPicker] = createSignal<SelectionInfo | null>(null)
  const [globalCommentOpen, setGlobalCommentOpen] = createSignal(false)

  // ─── Effects ──────────────────────────────────────────────────────

  // Fetch plan history when overlay opens
  createEffect(() => {
    if (isOpen()) {
      fetch('/api/plans')
        .then((r) => r.json())
        .then((plans: PlanSummary[]) => setPlanHistory(plans))
        .catch(() => {})
    }
  })

  // Keyboard shortcuts
  createEffect(() => {
    if (!isOpen()) return
    const handleKeyDown = (e: KeyboardEvent): void => {
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
        closePlan()
        return
      }
      // Cmd/Ctrl+Enter = Execute plan
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
    if (!isOpen()) return
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

  // ─── Helpers ──────────────────────────────────────────────────────

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

  const collectStepComments = (): Record<string, string> => {
    const comments = stepComments()
    const result: Record<string, string> = {}
    for (const [k, v] of Object.entries(comments)) {
      if (v) result[k] = v
    }
    return result
  }

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

  const handleApprove = (): void => {
    const plan = activePlan()
    if (!plan) return
    agent.resolvePlan('approved', plan, undefined, collectStepComments())
    executePlan()
  }

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

  const scrollToStep = (stepId: string): void => {
    setActiveStepId(stepId)
    const el = document.getElementById(`plan-step-${stepId}`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    }
  }

  // ─── Mode decision tree ───────────────────────────────────────────

  const handleTextSelected = (text: string, rect: DOMRect): void => {
    // Clear all previous floating UI
    setSelectionToolbar(null)
    setCommentPopover(null)
    setQuickLabelPicker(null)

    const mode = editorMode()
    switch (mode) {
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

  // ─── Render ───────────────────────────────────────────────────────

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
          <PlanHeader
            codename={plan().codename}
            copied={copied()}
            shareCopied={shareCopied()}
            hasDiff={hasDiff()}
            showDiff={showDiff()}
            onBack={() => closePlan()}
            onApprove={handleApprove}
            onSendFeedback={handleReject}
            onCopy={handleCopy}
            onDownload={handleDownload}
            onShare={handleShare}
            onClose={() => closePlan()}
            onToggleDiff={toggleDiff}
          />

          {/* 3-Panel body */}
          <div class="flex flex-1 overflow-hidden">
            <TOCSidebar
              steps={plan().steps}
              activeStepId={activeStepId()}
              collapsed={tocCollapsed()}
              planHistory={planHistory()}
              onScrollTo={scrollToStep}
              onToggleCollapse={() => setTocCollapsed((prev) => !prev)}
              onLoadPlan={loadPlanFromHistory}
            />

            {/* Center: Document area */}
            <main class="flex-1 overflow-y-auto" style={{ background: 'var(--bg)' }}>
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
                plan={plan()}
                annotations={annotations()}
                inputMethod={inputMethod()}
                showDiff={showDiff()}
                previousPlan={previousPlan()}
                onMouseUp={handleDocumentMouseUp}
                onTextSelected={handleTextSelected}
                onGlobalComment={() => setGlobalCommentOpen(true)}
                onCopyPlan={handleCopy}
                cardRef={() => {}}
              />
            </main>

            <AnnotationsPanel
              annotations={annotations()}
              focusedId={focusedAnnotationId()}
              onFocus={(id) => setFocusedAnnotationId(id)}
              onRemove={(id) => removeAnnotation(id)}
            />
          </div>

          {/* Floating Selection Toolbar (Markup mode only) */}
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
        </div>
      )}
    </Show>
  )
}
