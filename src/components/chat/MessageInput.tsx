/**
 * Message Input Component
 *
 * Chat input with Goose-style layout:
 * - Send/cancel buttons inside the textarea
 * - Single unified strip below with model selector, toggles, and context info
 *
 * Sub-components live in ./message-input/ for modularity.
 */

import { formatCost } from '@ava/core'
import { Activity, MessageSquare } from 'lucide-solid'
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
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ModelBrowserDialog } from '../dialogs/model-browser/model-browser-dialog'
import { buildFullMessage, processImageFile, processTextFile } from './message-input/attachments'
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
  const { selectedModel, setSelectedModel, contextUsage, sessionTokenStats, messages } =
    sessionStore
  const { settings, cyclePermissionMode, updateSettings } = useSettings()
  const { modelBrowserOpen, openModelBrowser, closeModelBrowser } = useLayout()

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

  onCleanup(() => {
    if (resizeFrame !== undefined) cancelAnimationFrame(resizeFrame)
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

  const handleKeyDown = (e: KeyboardEvent) => {
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
        <InputTextArea
          input={input}
          onInput={(v) => {
            setInput(v)
            autoResize()
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
              <span class="tabular-nums">{percentage().toFixed(0)}%</span>
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
    </div>
  )
}
