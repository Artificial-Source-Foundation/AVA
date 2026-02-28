/**
 * Message Input Component
 *
 * Chat input with Goose-style layout:
 * - Send/cancel buttons inside the textarea
 * - Single unified strip below with model selector, toggles, and context info
 *
 * Sub-components live in ./message-input/ for modularity.
 */

import { Activity, AlertCircle, AlertTriangle, MessageSquare } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { formatCost } from '../../lib/cost'
import type { SearchableFile } from '../../services/file-search'
import { filterFiles, getProjectFiles } from '../../services/file-search'
import { useDiagnostics } from '../../stores/diagnostics'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ModelBrowserDialog } from '../dialogs/model-browser/model-browser-dialog'
import { ExpandedEditor } from './ExpandedEditor'
import { buildFullMessage, processImageFile, processTextFile } from './message-input/attachments'
import { FileMentionPopover } from './message-input/file-mention-popover'
import { ModelSelector } from './message-input/model-selector'
import { InputTextArea } from './message-input/text-area'
import { PermissionBadge, PlanActSlider, ThinkingToggle } from './message-input/toolbar-buttons'
import {
  MAX_FILES,
  MAX_IMAGES,
  PASTE_LINE_THRESHOLD,
  type PendingFile,
  type PendingImage,
  type PendingPaste,
} from './message-input/types'
import { ShortcutHint } from './ShortcutHint'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const fmt = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

/** Thin vertical divider between strip groups */
const StripDivider: Component = () => <span class="w-px h-4 bg-[var(--border-subtle)] shrink-0" />

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const MessageInput: Component = () => {
  // State
  const [input, setInput] = createSignal('')
  const [sendCount, setSendCount] = createSignal(0)
  const [pendingImages, setPendingImages] = createSignal<PendingImage[]>([])
  const [pendingFiles, setPendingFiles] = createSignal<PendingFile[]>([])
  const [pendingPastes, setPendingPastes] = createSignal<PendingPaste[]>([])
  const [expandedPasteIndex, setExpandedPasteIndex] = createSignal<number | null>(null)
  const [isDragging, setIsDragging] = createSignal(false)
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)
  let submitting = false
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

  // Hooks / stores
  const chat = useChat()
  const agent = useAgent()
  const sessionStore = useSession()
  const { currentProject } = useProject()
  const { selectedModel, setSelectedModel, contextUsage, sessionTokenStats, messages } =
    sessionStore
  const { settings, cyclePermissionMode, updateSettings } = useSettings()
  const {
    modelBrowserOpen,
    openModelBrowser,
    closeModelBrowser,
    expandedEditorOpen,
    setExpandedEditorOpen,
  } = useLayout()
  const { diagnostics, hasDiagnostics } = useDiagnostics()

  // @ mention state
  const [mentionOpen, setMentionOpen] = createSignal(false)
  const [mentionQuery, setMentionQuery] = createSignal('')
  const [mentionIndex, setMentionIndex] = createSignal(0)
  const [mentionStart, setMentionStart] = createSignal(-1) // cursor position of @
  const [mentionFiles, setMentionFiles] = createSignal<SearchableFile[]>([])
  const mentionFiltered = createMemo(() =>
    mentionOpen() ? filterFiles(mentionFiles(), mentionQuery(), 12) : []
  )
  const projectDir = () => currentProject()?.directory

  // Preload project files when project changes
  createEffect(
    on(
      () => projectDir(),
      async (dir) => {
        if (!dir) return
        const files = await getProjectFiles(dir)
        setMentionFiles(files)
      }
    )
  )

  // Derived state
  const isProcessing = () => chat.isStreaming() || agent.isRunning()
  const inputHasText = createMemo(() => !!input().trim())

  const enabledProviders = createMemo(() =>
    settings().providers.filter((p) => p.enabled && p.models.length > 0)
  )

  // Auto-select a valid model when current selection doesn't match any enabled provider
  createEffect(() => {
    const providers = enabledProviders()
    if (providers.length === 0) return
    const modelId = selectedModel()
    const modelExists = providers.some((p) => p.models.some((m) => m.id === modelId))
    if (!modelExists) {
      const first = providers[0]
      const defaultModel = first.defaultModel || first.models[0]?.id
      if (defaultModel) setSelectedModel(defaultModel)
    }
  })

  const currentModelDisplay = createMemo(() => {
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return model.name
    }
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId
  })

  // Thinking mode support
  const modelSupportsThinking = createMemo(() => {
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return model.capabilities?.includes('thinking') ?? false
    }
    return false
  })

  const toggleThinking = () => {
    updateSettings({
      generation: {
        ...settings().generation,
        thinkingEnabled: !settings().generation.thinkingEnabled,
      },
    })
  }

  // Context bar state
  const showTokens = () => settings().ui.showTokenCount
  const toggleTokens = () => {
    updateSettings({ ui: { ...settings().ui, showTokenCount: !showTokens() } })
  }

  const tokenDisplay = () => {
    const real = sessionTokenStats().total
    if (real > 0) return fmt(real)
    return fmt(contextUsage().used)
  }

  const percentage = () => {
    const real = sessionTokenStats().total
    const limit = contextUsage().total
    if (real > 0 && limit > 0) return Math.min(100, (real / limit) * 100)
    return contextUsage().percentage
  }

  const barColor = () => {
    const pct = percentage()
    if (pct > 80) return 'var(--warning)'
    if (pct > 60) return 'var(--text-muted)'
    return 'var(--accent)'
  }

  const msgCount = () => messages().length

  // Effects
  createEffect(
    on(
      () => chat.streamingStartedAt(),
      (startedAt) => {
        if (!startedAt) {
          setElapsedSeconds(0)
          return
        }
        setElapsedSeconds(0)
        const interval = setInterval(() => {
          setElapsedSeconds(Math.floor((Date.now() - startedAt) / 1000))
        }, 1000)
        onCleanup(() => clearInterval(interval))
      }
    )
  )

  createEffect(
    on(
      () => sessionStore.currentSession()?.id,
      () => {
        chat.clearQueue()
      }
    )
  )

  // Listen for external input setting (from templates, etc.)
  const handleExternalInput = (e: Event) => {
    const text = (e as CustomEvent<{ text: string }>).detail.text
    setInput(text)
    queueMicrotask(() => {
      textareaRef?.focus()
      textareaRef?.setSelectionRange(text.length, text.length)
      autoResize()
    })
  }
  window.addEventListener('ava:set-input', handleExternalInput)

  onCleanup(() => {
    if (resizeFrame !== undefined) cancelAnimationFrame(resizeFrame)
    window.removeEventListener('ava:set-input', handleExternalInput)
  })

  // Attachment handlers
  const addImages = async (files: File[]) => {
    const remaining = MAX_IMAGES - pendingImages().length
    if (remaining <= 0) return
    const results = await Promise.all(files.slice(0, remaining).map(processImageFile))
    const valid = results.filter((r): r is NonNullable<typeof r> => r !== null)
    if (valid.length > 0) setPendingImages((prev) => [...prev, ...valid])
  }

  const addTextFiles = async (files: File[]) => {
    const remaining = MAX_FILES - pendingFiles().length
    if (remaining <= 0) return
    const results = await Promise.all(files.slice(0, remaining).map(processTextFile))
    const valid = results.filter((r): r is NonNullable<typeof r> => r !== null)
    if (valid.length > 0) setPendingFiles((prev) => [...prev, ...valid])
  }

  // Event handlers
  const autoResize = () => {
    if (!textareaRef) return
    if (resizeFrame !== undefined) cancelAnimationFrame(resizeFrame)
    resizeFrame = requestAnimationFrame(() => {
      if (!textareaRef) return
      const h = `${Math.min(textareaRef.scrollHeight, 200)}px`
      if (textareaRef.style.height !== h) textareaRef.style.height = h
      resizeFrame = undefined
    })
  }

  const handlePaste = (e: ClipboardEvent) => {
    const items = e.clipboardData?.items
    if (!items) return
    const imageFiles: File[] = []
    for (const item of items) {
      if (item.type.startsWith('image/')) {
        const file = item.getAsFile()
        if (file) imageFiles.push(file)
      }
    }
    if (imageFiles.length > 0) {
      e.preventDefault()
      addImages(imageFiles)
      return
    }
    const text = e.clipboardData?.getData('text/plain')
    if (text) {
      const lines = text.split('\n')
      if (lines.length > PASTE_LINE_THRESHOLD) {
        e.preventDefault()
        setPendingPastes((prev) => [...prev, { content: text, lineCount: lines.length }])
      }
    }
  }

  const handleDrop = (e: DragEvent) => {
    e.preventDefault()
    setIsDragging(false)
    const files = e.dataTransfer?.files
    if (!files) return
    const all = Array.from(files)
    const imgs = all.filter((f) => f.type.startsWith('image/'))
    const txts = all.filter((f) => !f.type.startsWith('image/'))
    if (imgs.length > 0) addImages(imgs)
    if (txts.length > 0) addTextFiles(txts)
  }

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    const message = input().trim()
    if (!message || submitting) return
    if (isProcessing()) return
    submitting = true
    setSendCount((c) => c + 1)
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    chat.clearError()
    agent.clearError()
    setPendingImages([])
    const files = pendingFiles()
    setPendingFiles([])
    const pastes = pendingPastes()
    setPendingPastes([])
    setExpandedPasteIndex(null)
    const fullMessage = buildFullMessage(message, files, pastes)
    try {
      await agent.run(fullMessage, { model: selectedModel() })
    } finally {
      submitting = false
    }
  }

  /** Detect @ mentions in the input text around the cursor */
  const checkMention = (value: string, cursorPos: number) => {
    // Scan backwards from cursor for @
    let atPos = -1
    for (let i = cursorPos - 1; i >= 0; i--) {
      const ch = value[i]
      if (ch === '@') {
        // @ must be at start or preceded by whitespace
        if (i === 0 || /\s/.test(value[i - 1])) {
          atPos = i
        }
        break
      }
      if (/\s/.test(ch)) break // whitespace before finding @ — no mention
    }

    if (atPos >= 0) {
      const query = value.slice(atPos + 1, cursorPos)
      setMentionOpen(true)
      setMentionQuery(query)
      setMentionStart(atPos)
      setMentionIndex(0)
    } else {
      setMentionOpen(false)
    }
  }

  const handleMentionSelect = (file: SearchableFile) => {
    const start = mentionStart()
    if (start < 0) return
    const value = input()
    const before = value.slice(0, start)
    const cursorEnd = start + 1 + mentionQuery().length
    const after = value.slice(cursorEnd)
    const inserted = `@${file.relative} `
    setInput(before + inserted + after)
    setMentionOpen(false)
    textareaRef?.focus()
    // Set cursor position after inserted text
    const newPos = before.length + inserted.length
    queueMicrotask(() => {
      textareaRef?.setSelectionRange(newPos, newPos)
    })
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    // @ mention keyboard handling
    if (mentionOpen() && mentionFiltered().length > 0) {
      if (e.key === 'ArrowDown') {
        e.preventDefault()
        setMentionIndex((i) => Math.min(i + 1, mentionFiltered().length - 1))
        return
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault()
        setMentionIndex((i) => Math.max(i - 1, 0))
        return
      }
      if (e.key === 'Enter' || e.key === 'Tab') {
        e.preventDefault()
        const file = mentionFiltered()[mentionIndex()]
        if (file) handleMentionSelect(file)
        return
      }
      if (e.key === 'Escape') {
        e.preventDefault()
        setMentionOpen(false)
        return
      }
    }

    const sk = settings().behavior.sendKey
    if (sk === 'enter') {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        handleSubmit(e)
      }
    } else if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault()
      handleSubmit(e)
    }
  }

  const handleCancel = () => {
    agent.cancel()
  }

  const placeholder = () =>
    isProcessing()
      ? `Working... (turn ${agent.currentTurn()})`
      : agent.isPlanMode()
        ? 'Plan your approach...'
        : 'Ask anything...'

  // Render
  return (
    <div class="density-section-px density-section-py border-t border-[var(--border-subtle)]">
      <form onSubmit={handleSubmit} class="space-y-1.5">
        {/* @ mention autocomplete popover */}
        <div class="relative">
          <FileMentionPopover
            open={mentionOpen}
            files={mentionFiltered}
            onSelect={handleMentionSelect}
            selectedIndex={mentionIndex}
          />
        </div>
        <InputTextArea
          input={input}
          onInput={(v) => {
            setInput(v)
            autoResize()
            // Check for @ mention after input changes
            const cursor = textareaRef?.selectionStart ?? v.length
            checkMention(v, cursor)
          }}
          onKeyDown={handleKeyDown}
          onPaste={handlePaste}
          onDrop={handleDrop}
          isDragging={isDragging}
          setIsDragging={setIsDragging}
          disabled={isProcessing}
          placeholder={placeholder}
          textareaRef={(el) => {
            textareaRef = el
          }}
          pendingImages={pendingImages}
          onRemoveImage={(i) => setPendingImages((p) => p.filter((_, x) => x !== i))}
          pendingFiles={pendingFiles}
          onRemoveFile={(i) => setPendingFiles((p) => p.filter((_, x) => x !== i))}
          pendingPastes={pendingPastes}
          expandedPasteIndex={expandedPasteIndex}
          onTogglePastePreview={(i) => setExpandedPasteIndex((p) => (p === i ? null : i))}
          onRemovePaste={(i) => {
            setPendingPastes((p) => p.filter((_, x) => x !== i))
            if (expandedPasteIndex() === i) setExpandedPasteIndex(null)
          }}
          isProcessing={isProcessing}
          isStreaming={chat.isStreaming}
          elapsedSeconds={elapsedSeconds}
          onCancel={handleCancel}
          inputHasText={inputHasText}
        />
        <ShortcutHint sendCount={sendCount()} />

        {/* ── Unified strip ── */}
        <div class="flex items-center justify-between text-[10px] text-[var(--text-tertiary)] font-[var(--font-ui-mono)] select-none overflow-x-auto">
          {/* Left: model + thinking + plan/act + permission */}
          <div class="flex items-center gap-2">
            <ModelSelector onToggle={openModelBrowser} currentModelDisplay={currentModelDisplay} />

            <ThinkingToggle
              enabled={() => settings().generation.thinkingEnabled}
              onToggle={toggleThinking}
              available={modelSupportsThinking}
            />

            <StripDivider />

            <PlanActSlider
              isPlanMode={agent.isPlanMode}
              togglePlanMode={() => agent.togglePlanMode()}
              isProcessing={isProcessing}
            />

            <StripDivider />

            <PermissionBadge
              permissionMode={() => settings().permissionMode}
              onCyclePermission={cyclePermissionMode}
            />
          </div>

          {/* Right: token info */}
          <div class="flex items-center gap-1.5 shrink-0">
            <button
              type="button"
              onClick={toggleTokens}
              class="inline-flex items-center gap-1 hover:text-[var(--text-secondary)] transition-colors"
              title={showTokens() ? 'Hide token details' : 'Show token details'}
            >
              <Activity class="w-3 h-3" />
              <span class="tabular-nums">{tokenDisplay()}</span>
            </button>

            {/* Context warning icon at 80%+ */}
            <Show when={percentage() >= 80}>
              <span title={`Context ${percentage().toFixed(0)}% full`}>
                <AlertTriangle class="w-3 h-3 text-[var(--warning)]" />
              </span>
            </Show>

            {/* Progress bar + percentage (togglable) */}
            <Show when={showTokens()}>
              <div class="w-10 h-1 bg-[var(--surface-raised)] rounded-full overflow-hidden">
                <div
                  class="h-full rounded-full transition-all duration-300"
                  style={{
                    width: `${Math.min(100, percentage())}%`,
                    'background-color': barColor(),
                  }}
                />
              </div>
              <span
                class="tabular-nums"
                classList={{ 'text-[var(--warning)]': percentage() >= 80 }}
              >
                {percentage().toFixed(0)}%
              </span>
            </Show>

            {/* LSP diagnostics */}
            <Show when={hasDiagnostics()}>
              <span class="text-[var(--border-muted)]">&middot;</span>
              <span class="inline-flex items-center gap-1">
                <Show when={diagnostics().errors > 0}>
                  <span class="inline-flex items-center gap-0.5 text-[var(--error)]">
                    <AlertCircle class="w-2.5 h-2.5" />
                    {diagnostics().errors}
                  </span>
                </Show>
                <Show when={diagnostics().warnings > 0}>
                  <span class="inline-flex items-center gap-0.5 text-[var(--warning)]">
                    <AlertTriangle class="w-2.5 h-2.5" />
                    {diagnostics().warnings}
                  </span>
                </Show>
              </span>
            </Show>

            {/* Session cost */}
            <Show when={sessionTokenStats().totalCost > 0}>
              <span class="text-[var(--border-muted)]">&middot;</span>
              <span class="tabular-nums text-[var(--success)]">
                {formatCost(sessionTokenStats().totalCost)}
              </span>
            </Show>

            {/* Streaming token estimate */}
            <Show when={chat.isStreaming() && chat.streamingTokenEstimate() > 0}>
              <span class="text-[var(--border-muted)]">&middot;</span>
              <span class="text-[var(--accent)] animate-pulse tabular-nums">
                +{fmt(chat.streamingTokenEstimate())}
              </span>
            </Show>

            {/* Message count */}
            <Show when={msgCount() > 0}>
              <span class="text-[var(--border-muted)]">&middot;</span>
              <span class="inline-flex items-center gap-0.5 tabular-nums">
                <MessageSquare class="w-2.5 h-2.5" />
                {msgCount()}
              </span>
            </Show>
          </div>
        </div>
      </form>
      <ModelBrowserDialog
        open={modelBrowserOpen}
        onOpenChange={(open) => {
          if (!open) closeModelBrowser()
        }}
        selectedModel={selectedModel}
        onSelect={setSelectedModel}
        enabledProviders={enabledProviders}
      />
      <ExpandedEditor
        open={expandedEditorOpen()}
        initialText={input()}
        onApply={(text) => {
          setInput(text)
          setExpandedEditorOpen(false)
          queueMicrotask(() => {
            textareaRef?.focus()
            autoResize()
          })
        }}
        onClose={() => setExpandedEditorOpen(false)}
      />
    </div>
  )
}
