/**
 * Message Input Component
 *
 * Chat input with Goose-style layout:
 * - Send/cancel buttons inside the textarea
 * - Single unified strip below with model selector, toggles, and context info
 *
 * Sub-components live in ./message-input/ for modularity.
 */

import {
  Activity,
  AlertCircle,
  AlertTriangle,
  Archive,
  ExternalLink,
  Eye,
  EyeOff,
  Layers,
  MessageSquare,
  Shield,
} from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { formatCost } from '../../lib/cost'
import { getCoreBudget } from '../../services/core-bridge'
import type { SearchableFile } from '../../services/file-search'
import { filterFiles, getProjectFiles } from '../../services/file-search'
import { openInExternalEditor } from '../../services/ide-integration'
import { getStash, popStash, pushStash } from '../../services/prompt-stash'
import {
  type AudioAnalyserHandle,
  createAudioAnalyser,
  createDictation,
  getAudioDevices,
  isDictationSupported,
} from '../../services/voice-dictation'
import { useDiagnostics } from '../../stores/diagnostics'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSandbox } from '../../stores/sandbox'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ModelBrowserDialog } from '../dialogs/model-browser/model-browser-dialog'
import { SandboxReviewDialog } from '../dialogs/SandboxReviewDialog'
import { DoomLoopBanner } from './DoomLoopBanner'
import { ExpandedEditor } from './ExpandedEditor'
import { buildFullMessage, processImageFile, processTextFile } from './message-input/attachments'
import { FileMentionPopover } from './message-input/file-mention-popover'
import { ModelSelector } from './message-input/model-selector'
import { InputTextArea } from './message-input/text-area'
import {
  MicButton,
  PermissionBadge,
  PlanActSlider,
  ThinkingToggle,
} from './message-input/toolbar-buttons'
import {
  MAX_FILES,
  MAX_IMAGES,
  PASTE_LINE_THRESHOLD,
  type PendingFile,
  type PendingImage,
  type PendingPaste,
} from './message-input/types'
import { PlanBranchSelector } from './PlanBranchSelector'
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

  // Prompt history navigation (Item 1)
  const [historyIndex, setHistoryIndex] = createSignal(-1)
  const [savedDraft, setSavedDraft] = createSignal('')

  // Prompt stash (Item 3)
  const [stashSize, setStashSize] = createSignal(getStash().length)

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
  const sandbox = useSandbox()

  // Prompt history: reversed list of past user messages
  const promptHistory = createMemo(() =>
    messages()
      .filter((m) => m.role === 'user')
      .map((m) => m.content)
      .reverse()
  )

  // Voice dictation
  const [isRecording, setIsRecording] = createSignal(false)
  const dictationSupported = createMemo(() => isDictationSupported())
  const dictation = createDictation({
    onTranscript: (text) => {
      setInput((prev) => prev + text)
      queueMicrotask(autoResize)
    },
    onStateChange: setIsRecording,
    onError: (err) => console.warn('Voice dictation:', err),
  })

  // Audio analyser for waveform visualization (Feature 1.4)
  const [waveformBars, setWaveformBars] = createSignal<number[]>([0, 0, 0, 0, 0, 0, 0, 0])
  let analyserHandle: AudioAnalyserHandle | undefined
  let waveformRaf: number | undefined

  // Audio device list (Feature 1.5)
  const [audioDevices, setAudioDevices] = createSignal<MediaDeviceInfo[]>([])

  // Load audio devices on mount when dictation is supported
  if (dictationSupported()) {
    getAudioDevices()
      .then(setAudioDevices)
      .catch(() => {})
  }

  // Start/stop analyser when recording state changes
  createEffect(
    on(isRecording, (rec) => {
      if (rec) {
        const deviceId = settings().behavior.voiceDeviceId || undefined
        createAudioAnalyser(deviceId)
          .then((handle) => {
            analyserHandle = handle
            const tick = () => {
              const data = handle.getFrequencyData()
              // Pick 8 evenly-spaced bins, normalize to 0..16
              const bars: number[] = []
              const step = Math.floor(data.length / 8)
              for (let i = 0; i < 8; i++) {
                bars.push(Math.round((data[i * step] / 255) * 16))
              }
              setWaveformBars(bars)
              waveformRaf = requestAnimationFrame(tick)
            }
            waveformRaf = requestAnimationFrame(tick)
          })
          .catch(() => {})
      } else {
        if (waveformRaf !== undefined) cancelAnimationFrame(waveformRaf)
        analyserHandle?.stop()
        analyserHandle = undefined
        setWaveformBars([0, 0, 0, 0, 0, 0, 0, 0])
      }
    })
  )

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
  const inputDisabled = () => isProcessing()
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

  // Listen for stash events from global shortcuts (Item 3)
  const handleStash = () => {
    const text = input().trim()
    if (!text) return
    pushStash(text)
    setInput('')
    setStashSize(getStash().length)
    queueMicrotask(autoResize)
  }
  const handleRestore = () => {
    const text = popStash()
    if (text) {
      setInput(text)
      setStashSize(getStash().length)
      queueMicrotask(autoResize)
    }
  }
  window.addEventListener('ava:stash-prompt', handleStash)
  window.addEventListener('ava:restore-prompt', handleRestore)

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
    window.removeEventListener('ava:stash-prompt', handleStash)
    window.removeEventListener('ava:restore-prompt', handleRestore)
    dictation?.stop()
    if (waveformRaf !== undefined) cancelAnimationFrame(waveformRaf)
    analyserHandle?.stop()
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
    setHistoryIndex(-1)
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

    // Prompt history: ArrowUp when input is empty or navigating
    if (e.key === 'ArrowUp' && !mentionOpen()) {
      const history = promptHistory()
      if (history.length === 0) return
      if (historyIndex() === -1 && input().trim() === '') {
        e.preventDefault()
        setSavedDraft(input())
        setHistoryIndex(0)
        setInput(history[0])
        return
      }
      if (historyIndex() >= 0 && historyIndex() < history.length - 1) {
        e.preventDefault()
        const newIdx = historyIndex() + 1
        setHistoryIndex(newIdx)
        setInput(history[newIdx])
        return
      }
    }
    if (e.key === 'ArrowDown' && historyIndex() >= 0 && !mentionOpen()) {
      e.preventDefault()
      if (historyIndex() === 0) {
        setHistoryIndex(-1)
        setInput(savedDraft())
      } else {
        const newIdx = historyIndex() - 1
        setHistoryIndex(newIdx)
        setInput(promptHistory()[newIdx])
      }
      return
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
      <Show when={agent.doomLoopDetected()}>
        <DoomLoopBanner
          onStop={() => agent.cancel()}
          onRetry={() => {
            agent.cancel()
          }}
          onSwitchModel={() => openModelBrowser()}
        />
      </Show>
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
            if (historyIndex() >= 0) setHistoryIndex(-1)
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
          disabled={inputDisabled}
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

            {/* Thinking visibility toggle (Item 2) */}
            <Show when={settings().generation.thinkingEnabled}>
              <button
                type="button"
                onClick={() =>
                  updateSettings({
                    ui: { ...settings().ui, hideThinking: !settings().ui.hideThinking },
                  })
                }
                class={`p-1 rounded-[var(--radius-md)] transition-colors ${
                  settings().ui.hideThinking
                    ? 'text-[var(--text-muted)] bg-transparent hover:bg-[var(--surface-raised)]'
                    : 'text-[var(--accent)] bg-[var(--accent-subtle)]'
                }`}
                title={settings().ui.hideThinking ? 'Show thinking blocks' : 'Hide thinking blocks'}
              >
                {settings().ui.hideThinking ? <EyeOff class="w-3 h-3" /> : <Eye class="w-3 h-3" />}
              </button>
            </Show>

            <StripDivider />

            <PlanActSlider
              isPlanMode={agent.isPlanMode}
              togglePlanMode={() => agent.togglePlanMode()}
              isProcessing={isProcessing}
            />

            {/* Plan branch management — only visible in plan mode */}
            <Show when={agent.isPlanMode()}>
              <PlanBranchSelector
                isPlanMode={agent.isPlanMode}
                messages={messages}
                onMessagesChange={(msgs) => sessionStore.setMessages(msgs)}
              />
            </Show>

            <StripDivider />

            <PermissionBadge
              permissionMode={() => settings().permissionMode}
              onCyclePermission={cyclePermissionMode}
            />

            {/* Sandbox mode toggle */}
            <StripDivider />
            <button
              type="button"
              onClick={() => sandbox.toggleSandbox()}
              class={`inline-flex items-center gap-1 p-1 rounded-[var(--radius-md)] transition-colors ${
                sandbox.sandboxEnabled()
                  ? 'text-[var(--warning)] bg-[var(--warning-subtle)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--surface-raised)]'
              }`}
              title={
                sandbox.sandboxEnabled()
                  ? 'Sandbox mode ON (changes are queued)'
                  : 'Enable sandbox mode'
              }
            >
              <Shield class="w-3 h-3" />
              <Show when={sandbox.sandboxEnabled()}>
                <span class="text-[10px]">Sandbox</span>
                <Show when={sandbox.pendingCount() > 0}>
                  <button
                    type="button"
                    onClick={(e) => {
                      e.stopPropagation()
                      sandbox.openReview()
                    }}
                    class="px-1 py-0.5 text-[9px] font-medium bg-[var(--warning)] text-white rounded-full min-w-[16px] text-center"
                    title={`${sandbox.pendingCount()} pending change(s) — click to review`}
                  >
                    {sandbox.pendingCount()}
                  </button>
                </Show>
              </Show>
            </button>

            {/* Run in Background button (plan mode + running) */}
            <Show
              when={agent.isPlanMode() && isProcessing() && !sessionStore.backgroundPlanActive()}
            >
              <StripDivider />
              <button
                type="button"
                onClick={() => sessionStore.startBackgroundPlan()}
                class="inline-flex items-center gap-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
                title="Continue plan execution in background"
              >
                <Layers class="w-2.5 h-2.5" />
                Background
              </button>
            </Show>

            {/* Background plan active badge */}
            <Show when={sessionStore.backgroundPlanActive()}>
              <StripDivider />
              <span class="inline-flex items-center gap-1 text-[var(--accent)]">
                <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
                <span class="text-[10px]">Plan running</span>
              </span>
            </Show>

            <Show when={!isRecording()}>
              <StripDivider />
              <button
                type="button"
                onClick={async () => {
                  try {
                    const result = await openInExternalEditor(input())
                    setInput(result)
                    queueMicrotask(autoResize)
                  } catch (err) {
                    console.warn('External editor:', err)
                  }
                }}
                class="p-1 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--surface-raised)] transition-colors"
                title="Edit prompt in external editor ($EDITOR)"
              >
                <ExternalLink class="w-3 h-3" />
              </button>
            </Show>

            <Show when={dictation}>
              <StripDivider />
              <MicButton
                isRecording={isRecording}
                onToggle={() => dictation!.toggle()}
                supported={dictationSupported}
              />

              {/* Waveform visualizer (Feature 1.4) */}
              <Show when={isRecording()}>
                <div class="flex items-center gap-[2px] w-[20px] h-[16px]">
                  <For each={waveformBars()}>
                    {(h) => (
                      <div
                        class="w-[2px] rounded-full bg-[var(--accent)] transition-[height] duration-75"
                        style={{ height: `${Math.max(2, h)}px` }}
                      />
                    )}
                  </For>
                </div>
              </Show>

              {/* Device picker (Feature 1.5) */}
              <Show when={audioDevices().length > 1}>
                <select
                  class="h-[18px] text-[10px] max-w-[80px] truncate bg-transparent border-none outline-none text-[var(--text-tertiary)] cursor-pointer"
                  style={{ 'font-family': 'var(--font-ui-mono)' }}
                  value={settings().behavior.voiceDeviceId}
                  onChange={(e) => {
                    updateSettings({
                      behavior: { ...settings().behavior, voiceDeviceId: e.currentTarget.value },
                    })
                  }}
                >
                  <option value="">Default mic</option>
                  <For each={audioDevices()}>
                    {(dev) => (
                      <option value={dev.deviceId}>
                        {dev.label || `Mic ${dev.deviceId.slice(0, 6)}`}
                      </option>
                    )}
                  </For>
                </select>
              </Show>
            </Show>
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

            {/* Stash indicator (Item 3) */}
            <Show when={stashSize() > 0}>
              <span
                class="inline-flex items-center gap-0.5 text-[var(--accent)]"
                title={`${stashSize()} stashed prompt(s) — Ctrl+Shift+R to restore`}
              >
                <Archive class="w-2.5 h-2.5" />
                <span class="tabular-nums">{stashSize()}</span>
              </span>
              <span class="text-[var(--border-muted)]">&middot;</span>
            </Show>

            {/* Context warning icon at 80%+ */}
            <Show when={percentage() >= 80}>
              <span title={`Context ${percentage().toFixed(0)}% full`}>
                <AlertTriangle class="w-3 h-3 text-[var(--warning)]" />
              </span>
            </Show>

            {/* Compact button (Item 4) */}
            <Show when={percentage() >= settings().generation.compactionThreshold}>
              <button
                type="button"
                onClick={async () => {
                  const budget = getCoreBudget()
                  if (!budget) return
                  const msgs = messages()
                  if (msgs.length <= 4) return
                  const coreMessages = msgs.map((m) => ({
                    id: m.id,
                    role: m.role as 'user' | 'assistant' | 'system',
                    content: m.content,
                  }))
                  const result = await budget.compact(coreMessages)
                  if (result.tokensSaved === 0) return
                  const keptIds = new Set(result.messages.map((m) => m.id))
                  sessionStore.setMessages(msgs.filter((m) => keptIds.has(m.id)))
                  budget.clear()
                  for (const m of result.messages) budget.addMessage(m.id, m.content)
                  window.dispatchEvent(
                    new CustomEvent('ava:compacted', {
                      detail: {
                        removed: result.originalCount - result.compactedCount,
                        tokensSaved: result.tokensSaved,
                      },
                    })
                  )
                }}
                class="text-[10px] text-[var(--warning)] hover:text-[var(--accent)] transition-colors"
                title="Compact context now"
              >
                Compact
              </button>
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
      <SandboxReviewDialog
        open={sandbox.reviewDialogOpen()}
        changes={sandbox.pendingChanges()}
        onApplySelected={async (paths) => {
          await sandbox.applySelectedChanges(paths)
          if (sandbox.pendingCount() === 0) sandbox.closeReview()
        }}
        onApplyAll={async () => {
          await sandbox.applyAllChanges()
          sandbox.closeReview()
        }}
        onRejectAll={() => {
          sandbox.rejectAllChanges()
          sandbox.closeReview()
        }}
        onClose={() => sandbox.closeReview()}
      />
    </div>
  )
}
