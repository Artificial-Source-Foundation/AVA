/**
 * Message Input Component
 *
 * Chat input with Goose-style layout:
 * - Send/cancel buttons inside the textarea
 * - Single unified strip below with model selector, toggles, and context info
 *
 * Sub-components live in ./message-input/ for modularity.
 */

import { ExternalLink, Eye, EyeOff, Layers, Shield } from 'lucide-solid'
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
import type { SearchableFile } from '../../services/file-search'
import { filterFiles, getProjectFiles } from '../../services/file-search'
import { openInExternalEditor } from '../../services/ide-integration'
import { getStash, popStash, pushStash } from '../../services/prompt-stash'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSandbox } from '../../stores/sandbox'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ModelBrowserDialog } from '../dialogs/model-browser/model-browser-dialog'
import { SandboxReviewDialog } from '../dialogs/SandboxReviewDialog'
import { DoomLoopBanner } from './DoomLoopBanner'
import { ExpandedEditor } from './ExpandedEditor'
import { createAttachmentState } from './message-input/attachment-bar'
import { buildFullMessage } from './message-input/attachments'
import { FileMentionPopover } from './message-input/file-mention-popover'
import { ModelSelector } from './message-input/model-selector'
import { StatusBar } from './message-input/status-bar'
import { InputTextArea } from './message-input/text-area'
import {
  cycleReasoningEffort,
  DelegationToggle,
  PermissionBadge,
  PlanActSlider,
  ReasoningDropdown,
} from './message-input/toolbar-buttons'
import { VoiceButton } from './message-input/voice-button'
import { PlanBranchSelector } from './PlanBranchSelector'
import { ShortcutHint } from './ShortcutHint'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Thin vertical divider between strip groups */
const StripDivider: Component = () => <span class="w-px h-4 bg-[var(--border-subtle)] shrink-0" />

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const MessageInput: Component = () => {
  // State
  const [input, setInput] = createSignal('')
  const [sendCount, setSendCount] = createSignal(0)
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)
  let submitting = false
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

  // Prompt history navigation
  const [historyIndex, setHistoryIndex] = createSignal(-1)
  const [savedDraft, setSavedDraft] = createSignal('')

  // Prompt stash
  const [stashSize, setStashSize] = createSignal(getStash().length)

  // Attachment state (extracted hook)
  const attachments = createAttachmentState()

  // Hooks / stores
  const chat = useChat()
  const agent = useAgent()
  const sessionStore = useSession()
  const { currentProject } = useProject()
  const { selectedModel, selectedProvider, setSelectedModel, messages } = sessionStore
  const { settings, cyclePermissionMode, updateSettings } = useSettings()
  const {
    modelBrowserOpen,
    openModelBrowser,
    closeModelBrowser,
    expandedEditorOpen,
    setExpandedEditorOpen,
  } = useLayout()
  const sandbox = useSandbox()

  // Prompt history: reversed list of past user messages
  const promptHistory = createMemo(() =>
    messages()
      .filter((m) => m.role === 'user')
      .map((m) => m.content)
      .reverse()
  )

  // @ mention state
  const [mentionOpen, setMentionOpen] = createSignal(false)
  const [mentionQuery, setMentionQuery] = createSignal('')
  const [mentionIndex, setMentionIndex] = createSignal(0)
  const [mentionStart, setMentionStart] = createSignal(-1)
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
    const provId = selectedProvider()
    if (provId) {
      const provider = settings().providers.find((p) => p.id === provId)
      const model = provider?.models.find((m) => m.id === modelId)
      if (provider && model) return `${provider.name} | ${model.name}`
    }
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return `${provider.name} | ${model.name}`
    }
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId
  })

  // Reasoning mode support
  const activeProviderId = createMemo(() => {
    const provId = selectedProvider()
    if (provId) return provId
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      if (provider.models.some((m) => m.id === modelId)) return provider.id
    }
    return null
  })

  const modelSupportsReasoning = createMemo(() => {
    const modelId = selectedModel()
    const provId = activeProviderId()
    if (provId) {
      const provider = settings().providers.find((p) => p.id === provId)
      const model = provider?.models.find((m) => m.id === modelId)
      if (model)
        return model.capabilities?.some((c) => c === 'thinking' || c === 'reasoning') ?? false
    }
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model)
        return model.capabilities?.some((c) => c === 'thinking' || c === 'reasoning') ?? false
    }
    return /claude|sonnet|opus|gpt-5|o3-|o4-|codex|gemini|deepseek-r/i.test(modelId)
  })

  const handleCycleReasoning = () => {
    const current = settings().generation.reasoningEffort
    const next = cycleReasoningEffort(current, activeProviderId() ?? undefined)
    updateSettings({
      generation: {
        ...settings().generation,
        reasoningEffort: next,
        thinkingEnabled: next !== 'off',
      },
    })
  }

  const toggleDelegation = () => {
    updateSettings({
      generation: {
        ...settings().generation,
        delegationEnabled: !settings().generation.delegationEnabled,
      },
    })
  }

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

  // Stash events
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

  // External input setting (from templates, etc.)
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
  })

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
    const { files, pastes } = attachments.clearAll()
    const fullMessage = buildFullMessage(message, files, pastes)
    try {
      await agent.run(fullMessage, { model: selectedModel() })
    } finally {
      submitting = false
    }
  }

  /** Detect @ mentions in the input text around the cursor */
  const checkMention = (value: string, cursorPos: number) => {
    let atPos = -1
    for (let i = cursorPos - 1; i >= 0; i--) {
      const ch = value[i]
      if (ch === '@') {
        if (i === 0 || /\s/.test(value[i - 1])) {
          atPos = i
        }
        break
      }
      if (/\s/.test(ch)) break
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

    // Prompt history
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
            const cursor = textareaRef?.selectionStart ?? v.length
            checkMention(v, cursor)
          }}
          onKeyDown={handleKeyDown}
          onPaste={attachments.handlePaste}
          onDrop={attachments.handleDrop}
          isDragging={attachments.isDragging}
          setIsDragging={attachments.setIsDragging}
          disabled={inputDisabled}
          placeholder={placeholder}
          textareaRef={(el) => {
            textareaRef = el
          }}
          pendingImages={attachments.pendingImages}
          onRemoveImage={attachments.removeImage}
          pendingFiles={attachments.pendingFiles}
          onRemoveFile={attachments.removeFile}
          pendingPastes={attachments.pendingPastes}
          expandedPasteIndex={attachments.expandedPasteIndex}
          onTogglePastePreview={attachments.togglePastePreview}
          onRemovePaste={attachments.removePaste}
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

            <ReasoningDropdown
              effort={() => settings().generation.reasoningEffort}
              onCycle={handleCycleReasoning}
              available={modelSupportsReasoning}
            />

            {/* Thinking visibility toggle */}
            <Show when={settings().generation.reasoningEffort !== 'off'}>
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

            <Show when={agent.isPlanMode()}>
              <PlanBranchSelector
                isPlanMode={agent.isPlanMode}
                messages={messages}
                onMessagesChange={(msgs) => sessionStore.setMessages(msgs)}
              />
            </Show>

            <DelegationToggle
              enabled={() => settings().generation.delegationEnabled}
              onToggle={toggleDelegation}
            />

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

            {/* Run in Background button */}
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

            <StripDivider />
            <VoiceButton
              onTranscript={(text) => {
                setInput((prev) => prev + text)
                queueMicrotask(autoResize)
              }}
            />
          </div>

          {/* Right: token info */}
          <StatusBar
            stashSize={stashSize}
            isStreaming={chat.isStreaming}
            streamingTokenEstimate={chat.streamingTokenEstimate}
          />
        </div>
      </form>
      <ModelBrowserDialog
        open={modelBrowserOpen}
        onOpenChange={(open) => {
          if (!open) closeModelBrowser()
        }}
        selectedModel={selectedModel}
        selectedProvider={selectedProvider}
        onSelect={(modelId, providerId) => setSelectedModel(modelId, providerId)}
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
