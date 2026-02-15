/**
 * Message Input Component
 *
 * Chat input with model selector, Plan/Act toggle, and Agent mode.
 * Bottom toolbar inspired by OpenCode Desktop.
 *
 * Sub-components live in ./message-input/ for modularity.
 */

import { type Component, createEffect, createMemo, createSignal, on, onCleanup } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { buildFullMessage, processImageFile, processTextFile } from './message-input/attachments'
import { ModelSelector } from './message-input/model-selector'
import { StatusBar } from './message-input/status-bar'
import { InputTextArea } from './message-input/text-area'
import { ToolbarButtons } from './message-input/toolbar-buttons'
import {
  MAX_FILES,
  MAX_IMAGES,
  PASTE_LINE_THRESHOLD,
  type PendingFile,
  type PendingImage,
  type PendingPaste,
} from './message-input/types'
import { ShortcutHint } from './ShortcutHint'

export const MessageInput: Component = () => {
  // State
  const [input, setInput] = createSignal('')
  const [useAgentMode, setUseAgentMode] = createSignal(false)
  const [modelDropdownOpen, setModelDropdownOpen] = createSignal(false)
  const [sendCount, setSendCount] = createSignal(0)
  const [pendingImages, setPendingImages] = createSignal<PendingImage[]>([])
  const [pendingFiles, setPendingFiles] = createSignal<PendingFile[]>([])
  const [pendingPastes, setPendingPastes] = createSignal<PendingPaste[]>([])
  const [expandedPasteIndex, setExpandedPasteIndex] = createSignal<number | null>(null)
  const [isDragging, setIsDragging] = createSignal(false)
  const [undoStatus, setUndoStatus] = createSignal<string | null>(null)
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)
  let submitting = false
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

  // Hooks / stores
  const chat = useChat()
  const agent = useAgent()
  const sessionStore = useSession()
  const { selectedModel, setSelectedModel } = sessionStore
  const { settings, cyclePermissionMode } = useSettings()

  // Derived state
  const isProcessing = () => chat.isStreaming() || agent.isRunning()
  const inputHasText = createMemo(() => !!input().trim())

  const enabledProviders = createMemo(() =>
    settings().providers.filter((p) => p.enabled && p.models.length > 0)
  )

  const currentModelDisplay = createMemo(() => {
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return model.name
    }
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId
  })

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
    const fullMessage = buildFullMessage(message, files, pastes)
    try {
      if (useAgentMode()) await agent.run(fullMessage, { model: selectedModel() })
      else
        await chat.sendMessage(fullMessage, selectedModel(), images.length > 0 ? images : undefined)
    } finally {
      submitting = false
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && e.ctrlKey && e.shiftKey && chat.isStreaming() && !useAgentMode()) {
      e.preventDefault()
      const msg = input().trim()
      if (!msg) return
      chat.steer(msg, selectedModel())
      setInput('')
      if (textareaRef) textareaRef.style.height = 'auto'
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
    if (useAgentMode()) agent.cancel()
    else chat.cancel()
  }

  const placeholder = () =>
    isProcessing()
      ? useAgentMode()
        ? `Working... (turn ${agent.currentTurn()})`
        : chat.queuedCount() > 0
          ? `${chat.queuedCount()} queued — type to add more...`
          : 'Type to queue follow-up...'
      : agent.isPlanMode()
        ? 'Plan your approach...'
        : 'Ask anything...'

  // Render
  return (
    <div class="density-section-px density-section-py border-t border-[var(--border-subtle)]">
      <form onSubmit={handleSubmit} class="space-y-2">
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
          disabled={() => useAgentMode() && isProcessing()}
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
        />
        <ShortcutHint sendCount={sendCount()} />
        <div class="flex items-center justify-between density-gap">
          <div class="flex items-center density-gap">
            <ModelSelector
              isOpen={modelDropdownOpen}
              onToggle={() => setModelDropdownOpen(!modelDropdownOpen())}
              onClose={() => setModelDropdownOpen(false)}
              onSelect={(id) => {
                setSelectedModel(id)
                setModelDropdownOpen(false)
              }}
              currentModelDisplay={currentModelDisplay}
              selectedModel={selectedModel}
              enabledProviders={enabledProviders}
            />
            <ToolbarButtons
              isPlanMode={agent.isPlanMode}
              togglePlanMode={() => agent.togglePlanMode()}
              useAgentMode={useAgentMode}
              onToggleAgentMode={() => setUseAgentMode(!useAgentMode())}
              isProcessing={isProcessing}
              permissionMode={() => settings().permissionMode}
              onCyclePermission={cyclePermissionMode}
              messageCount={() => sessionStore.messages().length}
              onCreateCheckpoint={async () => {
                const n = sessionStore.messages().length
                if (n > 0) await sessionStore.createCheckpoint(`Checkpoint at message #${n}`)
              }}
              gitEnabled={() => settings().git.enabled}
              autoCommit={() => settings().git.autoCommit}
              onUndo={async () => {
                setUndoStatus('Undoing...')
                const r = await chat.undoLastEdit()
                setUndoStatus(r.success ? 'Reverted!' : r.message)
                setTimeout(() => setUndoStatus(null), 2500)
              }}
              undoStatus={undoStatus}
              isUndoing={() => undoStatus() === 'Undoing...'}
            />
          </div>
          <StatusBar
            isProcessing={isProcessing}
            isStreaming={chat.isStreaming}
            elapsedSeconds={elapsedSeconds}
            streamingTokenEstimate={chat.streamingTokenEstimate}
            activeToolCallCount={() => chat.activeToolCalls().length}
            agentIsRunning={agent.isRunning}
            agentCurrentTurn={agent.currentTurn}
            doomLoopDetected={agent.doomLoopDetected}
            queuedCount={chat.queuedCount}
            onCancel={handleCancel}
            inputHasText={inputHasText}
            useAgentMode={useAgentMode}
          />
        </div>
      </form>
    </div>
  )
}
