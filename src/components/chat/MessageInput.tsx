/**
 * Message Input Component
 *
 * Chat input with model selector, Plan/Act toggle, and Agent mode.
 * Bottom toolbar inspired by OpenCode Desktop.
 */

import {
  ArrowUp,
  Bookmark,
  Bot,
  ChevronDown,
  ChevronUp,
  Clipboard,
  FileSearch,
  FileText,
  Image,
  Shield,
  ShieldAlert,
  ShieldOff,
  Square,
  Undo2,
  X,
  Zap,
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
import { useSession } from '../../stores/session'
import type { PermissionMode } from '../../stores/settings'
import { useSettings } from '../../stores/settings'
import { ShortcutHint } from './ShortcutHint'

// Vision constants
const MAX_IMAGE_SIZE = 5 * 1024 * 1024 // 5MB
const MAX_IMAGES = 4
const ACCEPTED_TYPES = ['image/png', 'image/jpeg', 'image/gif', 'image/webp']

// Paste collapse constants
const PASTE_LINE_THRESHOLD = 5
const PASTE_PREVIEW_LINES = 3

// File context constants
const MAX_FILE_SIZE = 100 * 1024 // 100KB
const MAX_FILES = 5
const TEXT_EXTENSIONS = new Set([
  '.ts',
  '.tsx',
  '.js',
  '.jsx',
  '.json',
  '.md',
  '.txt',
  '.css',
  '.html',
  '.py',
  '.rs',
  '.go',
  '.java',
  '.c',
  '.cpp',
  '.h',
  '.yml',
  '.yaml',
  '.toml',
  '.env',
  '.sh',
  '.bash',
  '.sql',
  '.graphql',
  '.xml',
  '.svg',
])

export const MessageInput: Component = () => {
  const [input, setInput] = createSignal('')
  const [useAgentMode, setUseAgentMode] = createSignal(false)
  const [modelDropdownOpen, setModelDropdownOpen] = createSignal(false)
  const [sendCount, setSendCount] = createSignal(0)
  const [pendingImages, setPendingImages] = createSignal<
    Array<{ data: string; mimeType: string; name?: string }>
  >([])
  const [pendingFiles, setPendingFiles] = createSignal<Array<{ name: string; content: string }>>([])
  const [pendingPastes, setPendingPastes] = createSignal<
    Array<{ content: string; lineCount: number }>
  >([])
  const [expandedPasteIndex, setExpandedPasteIndex] = createSignal<number | null>(null)
  const [isDragging, setIsDragging] = createSignal(false)
  let submitting = false
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

  // Chat mode (simple single-turn)
  const chat = useChat()

  // Agent mode (full autonomous loop)
  const agent = useAgent()

  // Session + Settings for model selection
  const sessionStore = useSession()
  const { selectedModel, setSelectedModel } = sessionStore
  const { settings, cyclePermissionMode } = useSettings()

  // Permission mode config
  const permissionConfig: Record<
    PermissionMode,
    { icon: typeof Shield; color: string; label: string }
  > = {
    ask: { icon: Shield, color: 'var(--text-muted)', label: 'Ask' },
    'auto-approve': { icon: ShieldAlert, color: 'var(--warning)', label: 'Auto' },
    bypass: { icon: ShieldOff, color: 'var(--error)', label: 'Bypass' },
  }

  // Get enabled providers and their models
  const enabledProviders = createMemo(() =>
    settings().providers.filter((p) => p.enabled && p.models.length > 0)
  )

  // Get the display name for the current model
  const currentModelDisplay = createMemo(() => {
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return model.name
    }
    // Fallback: show raw model ID trimmed
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId
  })

  const handleSelectModel = (modelId: string) => {
    setSelectedModel(modelId)
    setModelDropdownOpen(false)
  }

  // Checkpoint handler
  const handleCreateCheckpoint = async () => {
    const count = sessionStore.messages().length
    if (count === 0) return
    await sessionStore.createCheckpoint(`Checkpoint at message #${count}`)
  }

  // Undo handler — reverts last auto-committed AI edit
  const [undoStatus, setUndoStatus] = createSignal<string | null>(null)
  const handleUndo = async () => {
    setUndoStatus('Undoing...')
    const result = await chat.undoLastEdit()
    setUndoStatus(result.success ? 'Reverted!' : result.message)
    setTimeout(() => setUndoStatus(null), 2500)
  }

  // Vision: process a File into base64 image data
  const processImageFile = (
    file: File
  ): Promise<{ data: string; mimeType: string; name: string } | null> => {
    if (!ACCEPTED_TYPES.includes(file.type)) return Promise.resolve(null)
    if (file.size > MAX_IMAGE_SIZE) return Promise.resolve(null)
    return new Promise((resolve) => {
      const reader = new FileReader()
      reader.onload = () => {
        const result = reader.result as string
        const base64 = result.split(',')[1]
        if (base64) {
          resolve({ data: base64, mimeType: file.type, name: file.name })
        } else {
          resolve(null)
        }
      }
      reader.onerror = () => resolve(null)
      reader.readAsDataURL(file)
    })
  }

  const addImages = async (files: File[]) => {
    const current = pendingImages()
    const remaining = MAX_IMAGES - current.length
    if (remaining <= 0) return
    const toProcess = files.slice(0, remaining)
    const results = await Promise.all(toProcess.map(processImageFile))
    const valid = results.filter((r): r is NonNullable<typeof r> => r !== null)
    if (valid.length > 0) {
      setPendingImages((prev) => [...prev, ...valid])
    }
  }

  const removeImage = (index: number) => {
    setPendingImages((prev) => prev.filter((_, i) => i !== index))
  }

  const removeFile = (index: number) => {
    setPendingFiles((prev) => prev.filter((_, i) => i !== index))
  }

  /** Read a text file and return its content */
  const processTextFile = (file: File): Promise<{ name: string; content: string } | null> => {
    const ext = `.${file.name.split('.').pop()?.toLowerCase()}`
    if (!TEXT_EXTENSIONS.has(ext)) return Promise.resolve(null)
    if (file.size > MAX_FILE_SIZE) return Promise.resolve(null)
    return new Promise((resolve) => {
      const reader = new FileReader()
      reader.onload = () => resolve({ name: file.name, content: reader.result as string })
      reader.onerror = () => resolve(null)
      reader.readAsText(file)
    })
  }

  const addTextFiles = async (files: File[]) => {
    const current = pendingFiles()
    const remaining = MAX_FILES - current.length
    if (remaining <= 0) return
    const toProcess = files.slice(0, remaining)
    const results = await Promise.all(toProcess.map(processTextFile))
    const valid = results.filter((r): r is NonNullable<typeof r> => r !== null)
    if (valid.length > 0) {
      setPendingFiles((prev) => [...prev, ...valid])
    }
  }

  // Paste handler for images and large text blocks
  const handlePaste = (e: ClipboardEvent) => {
    const items = e.clipboardData?.items
    if (!items) return

    // Check for images first
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

    // Check for large text pastes
    const text = e.clipboardData?.getData('text/plain')
    if (text) {
      const lines = text.split('\n')
      if (lines.length > PASTE_LINE_THRESHOLD) {
        e.preventDefault()
        setPendingPastes((prev) => [...prev, { content: text, lineCount: lines.length }])
      }
    }
  }

  const removePaste = (index: number) => {
    setPendingPastes((prev) => prev.filter((_, i) => i !== index))
    if (expandedPasteIndex() === index) setExpandedPasteIndex(null)
  }

  const togglePastePreview = (index: number) => {
    setExpandedPasteIndex((prev) => (prev === index ? null : index))
  }

  const getPastePreview = (content: string) => {
    return content.split('\n').slice(0, PASTE_PREVIEW_LINES).join('\n')
  }

  // Drop handler for images and text files
  const handleDrop = (e: DragEvent) => {
    e.preventDefault()
    setIsDragging(false)
    const files = e.dataTransfer?.files
    if (!files) return
    const allFiles = Array.from(files)
    const imageFiles = allFiles.filter((f) => f.type.startsWith('image/'))
    const textFiles = allFiles.filter((f) => !f.type.startsWith('image/'))
    if (imageFiles.length > 0) addImages(imageFiles)
    if (textFiles.length > 0) addTextFiles(textFiles)
  }

  const handleDragOver = (e: DragEvent) => {
    e.preventDefault()
    setIsDragging(true)
  }

  const handleDragLeave = () => {
    setIsDragging(false)
  }

  const autoResize = () => {
    if (!textareaRef) return
    if (resizeFrame !== undefined) cancelAnimationFrame(resizeFrame)

    resizeFrame = requestAnimationFrame(() => {
      if (!textareaRef) return

      const nextHeight = `${Math.min(textareaRef.scrollHeight, 200)}px`
      if (textareaRef.style.height !== nextHeight) {
        textareaRef.style.height = nextHeight
      }

      resizeFrame = undefined
    })
  }

  onCleanup(() => {
    if (resizeFrame !== undefined) cancelAnimationFrame(resizeFrame)
  })

  const isProcessing = () => chat.isStreaming() || agent.isRunning()

  // Streaming elapsed timer
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)
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

  // Clear message queue on session switch
  createEffect(
    on(
      () => sessionStore.currentSession()?.id,
      () => {
        chat.clearQueue()
      }
    )
  )

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    const message = input().trim()
    if (!message || submitting) return
    if (useAgentMode() && isProcessing()) return

    submitting = true
    setSendCount((c) => c + 1)
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    chat.clearError()
    agent.clearError()

    const images = pendingImages()
    setPendingImages([])

    const files = pendingFiles()
    setPendingFiles([])

    const pastes = pendingPastes()
    setPendingPastes([])
    setExpandedPasteIndex(null)

    // Prepend file context as fenced code blocks
    let fullMessage = message
    if (files.length > 0) {
      const fileBlocks = files
        .map((f) => {
          const ext = f.name.split('.').pop() || ''
          return `**${f.name}:**\n\`\`\`${ext}\n${f.content}\n\`\`\``
        })
        .join('\n\n')
      fullMessage = `${fileBlocks}\n\n${fullMessage}`
    }

    // Append pasted text blocks
    if (pastes.length > 0) {
      const pasteBlocks = pastes.map((p) => `\`\`\`\n${p.content}\n\`\`\``).join('\n\n')
      fullMessage = `${fullMessage}\n\n${pasteBlocks}`
    }

    try {
      if (useAgentMode()) {
        await agent.run(fullMessage, { model: selectedModel() })
      } else {
        await chat.sendMessage(fullMessage, selectedModel(), images.length > 0 ? images : undefined)
      }
    } finally {
      submitting = false
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    // Steer: Ctrl+Shift+Enter while streaming in chat mode
    if (e.key === 'Enter' && e.ctrlKey && e.shiftKey && chat.isStreaming() && !useAgentMode()) {
      e.preventDefault()
      const message = input().trim()
      if (!message) return
      chat.steer(message, selectedModel())
      setInput('')
      if (textareaRef) textareaRef.style.height = 'auto'
      return
    }

    const sendKey = settings().behavior.sendKey
    if (sendKey === 'enter') {
      // Enter sends, Shift+Enter for newline
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        handleSubmit(e)
      }
    } else {
      // Ctrl+Enter sends, Enter for newline
      if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
        e.preventDefault()
        handleSubmit(e)
      }
    }
  }

  const handleCancel = () => {
    if (useAgentMode()) {
      agent.cancel()
    } else {
      chat.cancel()
    }
  }

  return (
    <div class="density-section-px density-section-py border-t border-[var(--border-subtle)]">
      {/* Input form */}
      <form onSubmit={handleSubmit} class="space-y-2">
        {/* biome-ignore lint/a11y/noStaticElementInteractions: drop zone for images and files */}
        <div
          class="relative"
          onDrop={handleDrop}
          onDragOver={handleDragOver}
          onDragLeave={handleDragLeave}
        >
          {/* Drag overlay */}
          <Show when={isDragging()}>
            <div class="absolute inset-0 z-10 flex items-center justify-center rounded-lg border-2 border-dashed border-[var(--accent)] bg-[var(--accent-subtle)]">
              <span class="text-xs font-medium text-[var(--accent)]">Drop files here</span>
            </div>
          </Show>

          {/* Pending image previews */}
          <Show when={pendingImages().length > 0}>
            <div class="flex gap-2 mb-2 flex-wrap px-3">
              <For each={pendingImages()}>
                {(img, i) => (
                  <div class="relative w-14 h-14 rounded overflow-hidden border border-[var(--border-subtle)]">
                    <img
                      src={`data:${img.mimeType};base64,${img.data}`}
                      alt={img.name || 'Preview'}
                      class="w-full h-full object-cover"
                    />
                    <button
                      type="button"
                      onClick={() => removeImage(i())}
                      class="absolute -top-1 -right-1 w-4 h-4 bg-[var(--error)] text-white rounded-full text-[10px] leading-none flex items-center justify-center"
                    >
                      <X class="w-2.5 h-2.5" />
                    </button>
                  </div>
                )}
              </For>
            </div>
          </Show>

          {/* Pending file chips */}
          <Show when={pendingFiles().length > 0}>
            <div class="flex gap-1.5 mb-2 flex-wrap px-3">
              <For each={pendingFiles()}>
                {(file, i) => (
                  <div class="flex items-center gap-1.5 px-2 py-1 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[11px] text-[var(--text-secondary)]">
                    <FileText class="w-3 h-3 text-[var(--text-muted)]" />
                    <span class="truncate max-w-[120px]">{file.name}</span>
                    <button
                      type="button"
                      onClick={() => removeFile(i())}
                      class="w-3.5 h-3.5 flex items-center justify-center rounded-full hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
                    >
                      <X class="w-2.5 h-2.5" />
                    </button>
                  </div>
                )}
              </For>
            </div>
          </Show>

          {/* Pending paste chips */}
          <Show when={pendingPastes().length > 0}>
            <div class="flex flex-col gap-1.5 mb-2 px-3">
              <For each={pendingPastes()}>
                {(paste, i) => (
                  <div>
                    <div class="flex items-center gap-1.5 px-2 py-1 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[11px] text-[var(--text-secondary)] w-fit">
                      <Clipboard class="w-3 h-3 text-[var(--text-muted)]" />
                      <button
                        type="button"
                        onClick={() => togglePastePreview(i())}
                        class="flex items-center gap-1 hover:text-[var(--accent)] transition-colors"
                      >
                        <span>
                          Pasted text · {paste.lineCount} line{paste.lineCount !== 1 ? 's' : ''}
                        </span>
                        <Show
                          when={expandedPasteIndex() === i()}
                          fallback={<ChevronDown class="w-3 h-3" />}
                        >
                          <ChevronUp class="w-3 h-3" />
                        </Show>
                      </button>
                      <button
                        type="button"
                        onClick={() => removePaste(i())}
                        class="w-3.5 h-3.5 flex items-center justify-center rounded-full hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
                      >
                        <X class="w-2.5 h-2.5" />
                      </button>
                    </div>
                    {/* Expandable preview */}
                    <Show when={expandedPasteIndex() === i()}>
                      <pre class="mt-1 ml-1 px-2 py-1.5 text-[10px] leading-tight bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] overflow-x-auto max-h-[120px] overflow-y-auto text-[var(--text-secondary)] font-[var(--font-ui-mono)]">
                        {getPastePreview(paste.content)}
                        <Show when={paste.lineCount > PASTE_PREVIEW_LINES}>
                          <span class="text-[var(--text-muted)]">
                            {'\n'}... {paste.lineCount - PASTE_PREVIEW_LINES} more lines
                          </span>
                        </Show>
                      </pre>
                    </Show>
                  </div>
                )}
              </For>
            </div>
          </Show>
          <textarea
            ref={textareaRef}
            value={input()}
            onInput={(e) => {
              setInput(e.currentTarget.value)
              autoResize()
            }}
            onKeyDown={handleKeyDown}
            onPaste={handlePaste}
            placeholder={
              isProcessing()
                ? useAgentMode()
                  ? `Working... (turn ${agent.currentTurn()})`
                  : chat.queuedCount() > 0
                    ? `${chat.queuedCount()} queued — type to add more...`
                    : 'Type to queue follow-up...'
                : agent.isPlanMode()
                  ? 'Plan your approach...'
                  : 'Ask anything...'
            }
            disabled={useAgentMode() && isProcessing()}
            rows={1}
            class="
              w-full density-section-px density-section-py
              bg-[var(--input-background)] text-[var(--text-primary)]
              placeholder-[var(--input-placeholder)]
              border border-[var(--input-border)] rounded-lg
              resize-none
              transition-colors
              focus:outline-none focus:border-[var(--input-border-focus)]
              disabled:opacity-50
            "
            style={{
              'min-height': '44px',
              'max-height': '200px',
              'font-size': 'var(--chat-font-size)',
            }}
          />
        </div>

        {/* Shortcut hint */}
        <ShortcutHint sendCount={sendCount()} />

        {/* Bottom toolbar */}
        <div class="flex items-center justify-between density-gap">
          <div class="flex items-center density-gap">
            {/* Model selector */}
            <div class="relative">
              <button
                type="button"
                onClick={() => setModelDropdownOpen(!modelDropdownOpen())}
                class="
                  flex items-center gap-1 px-2 py-1
                  text-[11px] text-[var(--text-secondary)]
                  bg-[var(--surface-raised)]
                  border border-[var(--border-subtle)]
                  rounded-[var(--radius-md)]
                  hover:border-[var(--accent-muted)]
                  transition-colors
                "
              >
                <ChevronDown class="w-3 h-3" />
                <span class="truncate max-w-[140px]">{currentModelDisplay()}</span>
              </button>

              {/* Model dropdown */}
              <Show when={modelDropdownOpen()}>
                {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop click to close dropdown */}
                {/* biome-ignore lint/a11y/useKeyWithClickEvents: backdrop does not need keyboard interaction */}
                <div class="fixed inset-0 z-40" onClick={() => setModelDropdownOpen(false)} />
                <div
                  class="absolute bottom-full left-0 mb-1 z-50 w-64 max-h-72 overflow-y-auto bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-lg)] shadow-xl"
                  style={{ transform: 'translateZ(0)' }}
                >
                  <For each={enabledProviders()}>
                    {(provider) => (
                      <div>
                        <div class="px-3 py-1.5 text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider sticky top-0 bg-[var(--surface-overlay)]">
                          {provider.name}
                        </div>
                        <For each={provider.models}>
                          {(model) => (
                            <button
                              type="button"
                              onClick={() => handleSelectModel(model.id)}
                              class={`
                                w-full text-left px-3 py-1.5
                                text-xs transition-colors
                                ${
                                  selectedModel() === model.id
                                    ? 'text-[var(--accent)] bg-[var(--accent-subtle)]'
                                    : 'text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-primary)]'
                                }
                              `}
                            >
                              {model.name}
                            </button>
                          )}
                        </For>
                      </div>
                    )}
                  </For>
                  <Show when={enabledProviders().length === 0}>
                    <div class="px-3 py-4 text-center text-xs text-[var(--text-muted)]">
                      No providers configured
                    </div>
                  </Show>
                </div>
              </Show>
            </div>

            {/* Plan/Act toggle */}
            <button
              type="button"
              onClick={() => agent.togglePlanMode()}
              disabled={isProcessing()}
              class={`
                flex items-center gap-1 px-2 py-1
                text-[11px] font-medium rounded-[var(--radius-md)]
                transition-colors
                ${
                  agent.isPlanMode()
                    ? 'bg-[var(--warning-subtle)] text-[var(--warning)] border border-[var(--warning)]'
                    : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)]'
                }
                disabled:opacity-50 disabled:cursor-not-allowed
              `}
            >
              <FileSearch class="w-3 h-3" />
              {agent.isPlanMode() ? 'Plan' : 'Act'}
            </button>

            {/* Agent/Chat toggle */}
            <button
              type="button"
              onClick={() => setUseAgentMode(!useAgentMode())}
              disabled={isProcessing()}
              class={`
                flex items-center gap-1 px-2 py-1
                text-[11px] font-medium rounded-[var(--radius-md)]
                transition-colors
                ${
                  useAgentMode()
                    ? 'bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent)]'
                    : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)]'
                }
                disabled:opacity-50 disabled:cursor-not-allowed
              `}
              title={
                useAgentMode() ? 'Agent mode: Full autonomous loop' : 'Chat mode: Simple responses'
              }
            >
              {useAgentMode() ? <Bot class="w-3 h-3" /> : <Zap class="w-3 h-3" />}
              {useAgentMode() ? 'Agent' : 'Chat'}
            </button>

            {/* Permission mode toggle */}
            {(() => {
              const mode = settings().permissionMode
              const cfg = permissionConfig[mode]
              const Icon = cfg.icon
              return (
                <button
                  type="button"
                  onClick={() => cyclePermissionMode()}
                  class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors"
                  title={`Permissions: ${cfg.label} (click to cycle)`}
                >
                  <Icon class="w-3 h-3" style={{ color: cfg.color }} />
                  <span style={{ color: cfg.color }}>{cfg.label}</span>
                </button>
              )
            })()}

            {/* Checkpoint button */}
            <Show when={sessionStore.messages().length > 0}>
              <button
                type="button"
                onClick={handleCreateCheckpoint}
                disabled={isProcessing()}
                class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors text-[var(--text-secondary)] disabled:opacity-50"
                title="Save checkpoint"
              >
                <Bookmark class="w-3 h-3" />
              </button>
            </Show>

            {/* Undo button — visible when git auto-commit is enabled */}
            <Show when={settings().git.enabled && settings().git.autoCommit}>
              <button
                type="button"
                onClick={handleUndo}
                disabled={isProcessing() || undoStatus() === 'Undoing...'}
                class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors text-[var(--text-secondary)] disabled:opacity-50"
                title="Undo last AI edit (git revert)"
              >
                <Undo2 class="w-3 h-3" />
              </button>
            </Show>

            {/* Undo status feedback */}
            <Show when={undoStatus()}>
              <span
                class={`text-[10px] font-medium ${undoStatus() === 'Reverted!' ? 'text-[var(--success)]' : 'text-[var(--text-muted)]'}`}
              >
                {undoStatus()}
              </span>
            </Show>

            {/* Image paste indicator */}
            <Show when={!useAgentMode()}>
              <span class="text-[var(--text-muted)]" title="Paste or drop images (Ctrl+V)">
                <Image class="w-3 h-3" />
              </span>
            </Show>
          </div>

          {/* Right side: status + send/cancel */}
          <div class="flex items-center density-gap">
            {/* Streaming stats */}
            <Show when={chat.isStreaming()}>
              <span class="flex items-center gap-1.5 text-[10px] text-[var(--text-tertiary)] tabular-nums">
                <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
                {elapsedSeconds()}s
                <Show when={chat.streamingTokenEstimate() > 0}>
                  <span class="text-[var(--border-muted)]">&middot;</span>~
                  {chat.streamingTokenEstimate().toLocaleString()} tokens
                </Show>
                <Show when={chat.activeToolCalls().length > 0}>
                  <span class="text-[var(--border-muted)]">&middot;</span>
                  {chat.activeToolCalls().length} tools
                </Show>
              </span>
            </Show>

            {/* Agent status indicators */}
            <Show when={agent.isRunning()}>
              <span class="flex items-center gap-1 text-[10px] text-[var(--text-tertiary)]">
                <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
                Turn {agent.currentTurn()}
              </span>
            </Show>
            <Show when={agent.doomLoopDetected()}>
              <span class="text-[10px] text-[var(--warning)]">Loop</span>
            </Show>

            {/* Queue badge */}
            <Show when={chat.queuedCount() > 0}>
              <span class="text-[10px] text-[var(--accent)] font-medium tabular-nums">
                {chat.queuedCount()} queued
              </span>
            </Show>

            {/* Cancel button */}
            <Show when={isProcessing()}>
              <button
                type="button"
                onClick={handleCancel}
                class="
                  p-2
                  bg-[var(--error)] hover:brightness-110
                  text-white
                  rounded-[var(--radius-md)]
                  transition-colors
                "
              >
                <Square class="w-4 h-4" />
              </button>
            </Show>

            {/* Send / Queue button */}
            <button
              type="submit"
              disabled={!input().trim() || (useAgentMode() && isProcessing())}
              class={`
                p-2 rounded-[var(--radius-md)] transition-colors
                disabled:opacity-30 disabled:cursor-not-allowed
                ${
                  !useAgentMode() && isProcessing()
                    ? 'bg-[var(--surface-raised)] border border-[var(--accent-border)] text-[var(--accent)] hover:bg-[var(--accent-subtle)]'
                    : 'bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white'
                }
              `}
              title={
                !useAgentMode() && isProcessing()
                  ? 'Queue message (Ctrl+Shift+Enter to steer)'
                  : 'Send message'
              }
            >
              <ArrowUp class="w-4 h-4" />
            </button>
          </div>
        </div>
      </form>
    </div>
  )
}
