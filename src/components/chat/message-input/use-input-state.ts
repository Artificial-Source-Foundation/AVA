import { type Accessor, createEffect, createMemo, createSignal, on, onCleanup } from 'solid-js'
import { useNotification } from '../../../contexts/notification'
import { useAgent } from '../../../hooks/useAgent'
import { useElapsedTimer } from '../../../hooks/useElapsedTimer'
import { generateMessageId } from '../../../lib/ids'
import type { CommandEntry } from '../../../services/command-resolver'
import { parseSlashCommand } from '../../../services/command-resolver'
import {
  applyCompactionResult,
  parseCompactFocus,
  requestConversationCompaction,
} from '../../../services/context-compaction'
import type { SearchableFile } from '../../../services/file-search'
import { openInExternalEditor } from '../../../services/ide-integration'
import { getStash, popStash, pushStash } from '../../../services/prompt-stash'
import { useLayout } from '../../../stores/layout'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { createAttachmentState } from './attachment-bar'
import { buildFullMessage } from './attachments'
import { useMentionState } from './use-mention-state'
import { type ModelState, useModelState } from './use-model-state'
import { useSlashState } from './use-slash-state'
export interface InputState extends ModelState {
  input: Accessor<string>
  setInput: (v: string) => void
  sendCount: Accessor<number>
  elapsedSeconds: Accessor<number>
  stashSize: Accessor<number>
  attachments: ReturnType<typeof createAttachmentState>
  isProcessing: Accessor<boolean>
  inputDisabled: Accessor<boolean>
  inputHasText: Accessor<boolean>
  placeholder: Accessor<string>
  escapeHint: Accessor<boolean>
  mentionOpen: Accessor<boolean>
  mentionFiltered: Accessor<SearchableFile[]>
  mentionIndex: Accessor<number>
  handleMentionSelect: (file: SearchableFile) => void
  slashOpen: Accessor<boolean>
  slashCommands: Accessor<CommandEntry[]>
  slashIndex: Accessor<number>
  handleSlashSelect: (cmd: CommandEntry) => void
  handleSubmit: (e: Event) => Promise<void>
  handleKeyDown: (e: KeyboardEvent) => void
  handleCancel: () => void
  handleQueueFromMenu: () => void
  handleInterruptFromMenu: () => void
  handlePostCompleteFromMenu: () => void
  handleExternalEditor: () => Promise<void>
  autoResize: () => void
  onTextareaInput: (v: string) => void
  setTextareaRef: (el: HTMLTextAreaElement) => void
  focusTextarea: () => void
  agent: ReturnType<typeof useAgent>
  sessionStore: ReturnType<typeof useSession>
  settingsStore: ReturnType<typeof useSettings>
}

export function useInputState(): InputState {
  const [input, setInput] = createSignal('')
  const [sendCount, setSendCount] = createSignal(0)
  const [historyIndex, setHistoryIndex] = createSignal(-1)
  const [savedDraft, setSavedDraft] = createSignal('')
  const [stashSize, setStashSize] = createSignal(getStash().length)
  let submitting = false
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

  // Double-Escape abort: first press shows hint, second within 2s cancels
  let lastEscapeTime = 0
  let escapeTimer: ReturnType<typeof setTimeout> | undefined
  const [escapeHint, setEscapeHint] = createSignal(false)

  const attachments = createAttachmentState()
  const agent = useAgent()
  const notify = useNotification()
  const sessionStore = useSession()
  const layout = useLayout()

  const { selectedModel, messages } = sessionStore
  const settingsStore = useSettings()
  const { settings } = settingsStore
  const modelState = useModelState()
  const mention = useMentionState()
  const slash = useSlashState()
  const setTextareaRef = (el: HTMLTextAreaElement): void => {
    textareaRef = el
  }
  const focusTextarea = (): void => {
    textareaRef?.focus()
  }
  const promptHistory = createMemo(() =>
    messages()
      .filter((m) => m.role === 'user')
      .map((m) => m.content)
      .reverse()
  )
  const isProcessing = createMemo(() => agent.isRunning())
  // Input is NOT disabled during processing -- mid-stream messaging allows
  // users to type while the agent runs (Enter = queue, Ctrl+Enter = interrupt)
  const inputDisabled = createMemo(() => false)
  const inputHasText = createMemo(
    () =>
      !!input().trim() ||
      attachments.pendingPastes().length > 0 ||
      attachments.pendingImages().length > 0
  )
  const placeholder = createMemo(() =>
    isProcessing()
      ? 'Type a message...'
      : agent.isPlanMode()
        ? 'Plan your approach...'
        : 'Ask anything...'
  )

  // Auto-resize textarea
  const autoResize = (): void => {
    if (!textareaRef) return
    if (resizeFrame !== undefined) cancelAnimationFrame(resizeFrame)
    resizeFrame = requestAnimationFrame(() => {
      if (!textareaRef) return
      // Reset to auto so scrollHeight recalculates for shrinking content
      textareaRef.style.height = 'auto'
      const h = `${Math.min(textareaRef.scrollHeight, 200)}px`
      textareaRef.style.height = h
      resizeFrame = undefined
    })
  }

  const appendQueuedUserMessage = (
    tier: 'queued' | 'post-complete',
    message: string,
    sessionId: string
  ): void => {
    sessionStore.addMessage({
      id: generateMessageId(tier === 'queued' ? 'queue' : 'post'),
      sessionId,
      role: 'user',
      content: message,
      createdAt: Date.now(),
      metadata: { tier },
    })
  }

  const handleQueuedRequestFailure = (message: string, error: unknown): void => {
    const detail = error instanceof Error ? error.message : String(error)
    setInput(message)
    queueMicrotask(() => {
      autoResize()
      focusTextarea()
    })
    notify.error('Queue failed', detail)
  }

  const queueFollowUpMessage = async (message: string, sessionId: string): Promise<void> => {
    await agent.followUp(message, sessionId)
    appendQueuedUserMessage('queued', message, sessionId)
    setInput('')
    setHistoryIndex(-1)
    if (textareaRef) textareaRef.style.height = 'auto'
  }

  const queuePostCompleteMessage = async (message: string, sessionId: string): Promise<void> => {
    await agent.postComplete(message, undefined, sessionId)
    appendQueuedUserMessage('post-complete', message, sessionId)
    setInput('')
    setHistoryIndex(-1)
    if (textareaRef) textareaRef.style.height = 'auto'
  }

  // Elapsed timer during streaming
  const elapsedSecondsFromTimer = useElapsedTimer(() => agent.streamingStartedAt())

  // Clear only local, non-backend-managed queue items on session change.
  createEffect(
    on(
      () => sessionStore.currentSession()?.id,
      (_, previousSessionId) => {
        if (!previousSessionId) return
        agent.clearQueue(false, previousSessionId)
        queueMicrotask(() => {
          if (!textareaRef || document.activeElement === textareaRef) return
          textareaRef.focus()
        })
      },
      { defer: true }
    )
  )

  // Stash events
  const handleStash = (): void => {
    const text = input().trim()
    if (!text) return
    pushStash(text)
    setInput('')
    setStashSize(getStash().length)
    queueMicrotask(autoResize)
  }
  const handleRestore = (): void => {
    const text = popStash()
    if (text) {
      setInput(text)
      setStashSize(getStash().length)
      queueMicrotask(autoResize)
    }
  }
  window.addEventListener('ava:stash-prompt', handleStash)
  window.addEventListener('ava:restore-prompt', handleRestore)

  // External input setting (templates, etc.)
  const handleExternalInput = (e: Event): void => {
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
    clearTimeout(escapeTimer)
    window.removeEventListener('ava:set-input', handleExternalInput)
    window.removeEventListener('ava:stash-prompt', handleStash)
    window.removeEventListener('ava:restore-prompt', handleRestore)
  })

  const handleSubmit = async (e: Event): Promise<void> => {
    e.preventDefault()
    const message = input().trim()
    const hasPastes = attachments.pendingPastes().length > 0
    const hasImages = attachments.pendingImages().length > 0
    if ((!message && !hasPastes && !hasImages) || submitting) return

    const parsed = parseSlashCommand(message)
    if (parsed) {
      if (parsed.name === 'compact') {
        if (isProcessing()) {
          notify.error('Compaction unavailable', 'Wait for the current response to finish')
          return
        }
        try {
          notify.info('Compacting conversation...', 'Summarizing older context')
          const result = await requestConversationCompaction(parseCompactFocus(parsed.args))
          applyCompactionResult(result, 'manual')
        } catch (err) {
          notify.error(
            'Compaction failed',
            err instanceof Error ? err.message : 'Unknown compaction error'
          )
        }
        setInput('')
        setHistoryIndex(-1)
        if (textareaRef) textareaRef.style.height = 'auto'
        return
      }
      if (parsed.name === 'later') {
        const laterMsg = parsed.args.trim()
        if (laterMsg) {
          const sessionId = sessionStore.currentSession()?.id ?? ''
          try {
            await queuePostCompleteMessage(laterMsg, sessionId)
          } catch (error) {
            handleQueuedRequestFailure(laterMsg, error)
          }
        } else {
          setInput('')
          setHistoryIndex(-1)
          if (textareaRef) textareaRef.style.height = 'auto'
        }
        return
      }
      if (parsed.name === 'queue') {
        const queue = agent.messageQueue()
        const sessionId = sessionStore.currentSession()?.id ?? ''
        const queueText =
          queue.length === 0
            ? 'No messages in queue.'
            : queue
                .map(
                  (q, i) =>
                    `${i + 1}. **[${(q as { tier?: string }).tier ?? 'pending'}]** ${q.content.slice(0, 120)}`
                )
                .join('\n')
        sessionStore.addMessage({
          id: generateMessageId('sys'),
          sessionId,
          role: 'system',
          content: `**Message Queue** (${queue.length} item${queue.length === 1 ? '' : 's'})\n\n${queueText}`,
          createdAt: Date.now(),
        })
        setInput('')
        setHistoryIndex(-1)
        if (textareaRef) textareaRef.style.height = 'auto'
        return
      }

      // Helper to clear input after handling a command
      const clearInput = (): void => {
        setInput('')
        setHistoryIndex(-1)
        if (textareaRef) textareaRef.style.height = 'auto'
      }

      if (parsed.name === 'clear') {
        sessionStore.setMessages([])
        clearInput()
        return
      }
      if (parsed.name === 'new') {
        clearInput()
        await sessionStore.createNewSession(parsed.args.trim() || undefined)
        return
      }
      if (parsed.name === 'sessions') {
        layout.toggleSessionSwitcher()
        clearInput()
        return
      }
      if (parsed.name === 'model') {
        if (parsed.args.trim()) {
          // If args provided, let the agent handle model switching
        } else {
          layout.toggleQuickModelPicker()
          clearInput()
          return
        }
      }
      if (parsed.name === 'theme') {
        layout.openSettings()
        queueMicrotask(() => {
          window.dispatchEvent(
            new CustomEvent('ava:settings-tab', { detail: { tab: 'appearance' } })
          )
        })
        clearInput()
        return
      }
      if (parsed.name === 'permissions') {
        layout.openSettings()
        queueMicrotask(() => {
          window.dispatchEvent(
            new CustomEvent('ava:settings-tab', { detail: { tab: 'permissions-trust' } })
          )
        })
        clearInput()
        return
      }
      if (parsed.name === 'think') {
        const thinkArg = parsed.args.trim().toLowerCase()
        const currentEnabled = settings().generation.thinkingEnabled
        const shouldEnable =
          thinkArg === 'show' ? true : thinkArg === 'hide' ? false : !currentEnabled
        settingsStore.updateSettings({
          generation: {
            ...settings().generation,
            thinkingEnabled: shouldEnable,
          },
        })
        notify.info('Thinking', shouldEnable ? 'Thinking enabled' : 'Thinking disabled')
        clearInput()
        return
      }
      if (parsed.name === 'export') {
        // Trigger the export-chat shortcut action (Ctrl+Shift+E)
        document.dispatchEvent(
          new KeyboardEvent('keydown', { key: 'E', ctrlKey: true, shiftKey: true, bubbles: true })
        )
        clearInput()
        return
      }
      if (parsed.name === 'copy') {
        const msgs = messages()
        const lastAssistant = [...msgs].reverse().find((m) => m.role === 'assistant')
        if (lastAssistant) {
          void navigator.clipboard.writeText(lastAssistant.content).then(() => {
            notify.info('Copied', 'Response copied to clipboard')
          })
        } else {
          notify.info('Nothing to copy', 'No assistant response found')
        }
        clearInput()
        return
      }
      if (parsed.name === 'help') {
        const sessionId = sessionStore.currentSession()?.id ?? ''
        sessionStore.addMessage({
          id: generateMessageId('sys'),
          sessionId,
          role: 'system',
          content: [
            '**Available Commands**\n',
            '| Command | Description |',
            '|---------|-------------|',
            '| `/model` | Show or switch model |',
            '| `/think [show\\|hide]` | Toggle thinking visibility |',
            '| `/theme` | Open theme settings |',
            '| `/permissions` | Open permissions settings |',
            '| `/new [title]` | Start a new session |',
            '| `/sessions` | Open session picker |',
            '| `/commit` | Inspect commit readiness |',
            '| `/export` | Export conversation |',
            '| `/copy` | Copy last response |',
            '| `/compact [focus]` | Compact conversation |',
            '| `/later <msg>` | Queue post-complete message |',
            '| `/queue` | Show message queue |',
            '| `/clear` | Clear chat |',
            '| `/shortcuts` | Show keyboard shortcuts |',
            '| `/settings` | Open settings |',
            '| `/btw <question>` | Side conversation |',
            '| `/rewind` | Checkpoint history |',
          ].join('\n'),
          createdAt: Date.now(),
        })
        clearInput()
        return
      }
      if (parsed.name === 'shortcuts') {
        const sessionId = sessionStore.currentSession()?.id ?? ''
        sessionStore.addMessage({
          id: generateMessageId('sys'),
          sessionId,
          role: 'system',
          content: [
            '**Keyboard Shortcuts**\n',
            '| Shortcut | Action |',
            '|----------|--------|',
            '| `Ctrl+/` or `Ctrl+K` | Command palette |',
            '| `Ctrl+N` | New chat |',
            '| `Ctrl+L` | Session switcher |',
            '| `Ctrl+M` | Quick model picker |',
            '| `Ctrl+Shift+M` | Model browser |',
            '| `Ctrl+S` | Toggle sidebar |',
            '| `Ctrl+T` | Cycle thinking level |',
            '| `Ctrl+J` | Toggle bottom panel |',
            '| `Ctrl+,` | Open settings |',
            '| `Ctrl+E` | Expanded editor |',
            '| `Ctrl+F` | Search chat |',
            '| `Ctrl+Y` | Copy last response |',
            '| `Ctrl+Shift+E` | Export chat |',
            '| `Ctrl+Enter` | Interrupt & send |',
            '| `Alt+Enter` | Post-complete message |',
            '| `Double-Escape` | Cancel agent |',
          ].join('\n'),
          createdAt: Date.now(),
        })
        clearInput()
        return
      }
      if (parsed.name === 'settings') {
        layout.openSettings()
        clearInput()
        return
      }
    }

    // Mid-stream messaging: queue for next turn when agent is running (Enter)
    if (isProcessing()) {
      const sessionId = sessionStore.currentSession()?.id ?? ''
      void queueFollowUpMessage(message, sessionId).catch((error) => {
        handleQueuedRequestFailure(message, error)
      })
      return
    }

    submitting = true
    setSendCount((c) => c + 1)
    setInput('')
    setHistoryIndex(-1)
    if (textareaRef) textareaRef.style.height = 'auto'
    agent.clearError()
    const { files, pastes } = attachments.clearAll()
    try {
      await agent.run(buildFullMessage(message, files, pastes), { model: selectedModel() })
    } finally {
      submitting = false
    }
  }

  // Keydown — mention, history, send
  const handleKeyDown = (e: KeyboardEvent): void => {
    // Slash command popover navigation
    if (slash.slashOpen() && slash.slashCommands().length > 0) {
      if (e.key === 'ArrowDown') {
        e.preventDefault()
        slash.setSlashIndex((i: number) => Math.min(i + 1, slash.slashCommands().length - 1))
        return
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault()
        slash.setSlashIndex((i: number) => Math.max(i - 1, 0))
        return
      }
      if (e.key === 'Enter' || e.key === 'Tab') {
        e.preventDefault()
        const cmd = slash.slashCommands()[slash.slashIndex()]
        if (cmd) slash.handleSlashSelect(cmd, input, setInput, textareaRef)
        return
      }
      if (e.key === 'Escape') {
        e.preventDefault()
        slash.setSlashOpen(false)
        return
      }
    }
    if (mention.mentionOpen() && mention.mentionFiltered().length > 0) {
      if (e.key === 'ArrowDown') {
        e.preventDefault()
        mention.setMentionIndex((i: number) =>
          Math.min(i + 1, mention.mentionFiltered().length - 1)
        )
        return
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault()
        mention.setMentionIndex((i: number) => Math.max(i - 1, 0))
        return
      }
      if (e.key === 'Enter' || e.key === 'Tab') {
        e.preventDefault()
        const f = mention.mentionFiltered()[mention.mentionIndex()]
        if (f) mention.handleMentionSelect(f, input, setInput, textareaRef)
        return
      }
      if (e.key === 'Escape') {
        e.preventDefault()
        mention.setMentionOpen(false)
        return
      }
    }
    if (e.key === 'ArrowUp' && !mention.mentionOpen()) {
      const h = promptHistory()
      if (h.length === 0) return
      if (historyIndex() === -1 && input().trim() === '') {
        e.preventDefault()
        setSavedDraft(input())
        setHistoryIndex(0)
        setInput(h[0])
        return
      }
      if (historyIndex() >= 0 && historyIndex() < h.length - 1) {
        e.preventDefault()
        const i = historyIndex() + 1
        setHistoryIndex(i)
        setInput(h[i])
        return
      }
    }
    if (e.key === 'ArrowDown' && historyIndex() >= 0 && !mention.mentionOpen()) {
      e.preventDefault()
      if (historyIndex() === 0) {
        setHistoryIndex(-1)
        setInput(savedDraft())
      } else {
        const i = historyIndex() - 1
        setHistoryIndex(i)
        setInput(promptHistory()[i])
      }
      return
    }
    // Double-Escape abort (while agent is running)
    if (isProcessing() && e.key === 'Escape') {
      e.preventDefault()
      const now = Date.now()
      if (now - lastEscapeTime < 2000) {
        // Second press within 2s — cancel
        clearTimeout(escapeTimer)
        setEscapeHint(false)
        lastEscapeTime = 0
        agent.cancel()
      } else {
        // First press — show hint
        lastEscapeTime = now
        setEscapeHint(true)
        clearTimeout(escapeTimer)
        escapeTimer = setTimeout(() => {
          setEscapeHint(false)
          lastEscapeTime = 0
        }, 2000)
      }
      return
    }

    if (!isProcessing() && e.key === 'Escape' && document.activeElement === textareaRef) {
      e.preventDefault()
      textareaRef?.blur()
      mention.setMentionOpen(false)
      slash.setSlashOpen(false)
      return
    }

    // Mid-stream messaging keybinds (while agent is running).
    // During processing, Enter queues for next turn (regardless of sendKey setting).
    // Ctrl+Enter interrupts. Alt+Enter queues post-complete.
    if (isProcessing() && e.key === 'Enter') {
      const message = input().trim()
      if (!message) {
        // Shift+Enter: allow newline even during processing
        if (e.shiftKey) return
        e.preventDefault()
        return
      }

      if (e.ctrlKey && !e.altKey) {
        // Ctrl+Enter: interrupt — stop at next tool boundary, send immediately
        e.preventDefault()
        setInput('')
        if (textareaRef) textareaRef.style.height = 'auto'
        const sessionId = sessionStore.currentSession()?.id ?? ''
        sessionStore.addMessage({
          id: generateMessageId('intr'),
          sessionId,
          role: 'user',
          content: message,
          createdAt: Date.now(),
          metadata: { tier: 'interrupt' },
        })
        agent.steer(message)
        return
      }
      if (e.altKey) {
        // Alt+Enter: post-complete — queued for after agent fully stops
        e.preventDefault()
        const sessionId = sessionStore.currentSession()?.id ?? ''
        void queuePostCompleteMessage(message, sessionId).catch((error) => {
          handleQueuedRequestFailure(message, error)
        })
        return
      }
      if (!e.shiftKey) {
        // Enter (no modifiers): queue — agent finishes current turn, then processes
        e.preventDefault()
        const sessionId = sessionStore.currentSession()?.id ?? ''
        void queueFollowUpMessage(message, sessionId).catch((error) => {
          handleQueuedRequestFailure(message, error)
        })
        return
      }
      // Shift+Enter: newline (fall through)
      return
    }

    const sk = settings().behavior.sendKey
    if (sk === 'enter') {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        void handleSubmit(e)
      }
    } else if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault()
      void handleSubmit(e)
    }
  }

  const handleCancel = (): void => {
    agent.cancel()
  }

  // Menu-driven mid-stream send helpers (for context menu on send button)
  const handleQueueFromMenu = (): void => {
    const message = input().trim()
    if (!message || !isProcessing()) return
    const sessionId = sessionStore.currentSession()?.id ?? ''
    void queueFollowUpMessage(message, sessionId).catch((error) => {
      handleQueuedRequestFailure(message, error)
    })
  }

  const handleInterruptFromMenu = (): void => {
    const message = input().trim()
    if (!message || !isProcessing()) return
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    const sessionId = sessionStore.currentSession()?.id ?? ''
    sessionStore.addMessage({
      id: generateMessageId('intr'),
      sessionId,
      role: 'user',
      content: message,
      createdAt: Date.now(),
      metadata: { tier: 'interrupt' },
    })
    agent.steer(message)
  }

  const handlePostCompleteFromMenu = (): void => {
    const message = input().trim()
    if (!message || !isProcessing()) return
    const sessionId = sessionStore.currentSession()?.id ?? ''
    void queuePostCompleteMessage(message, sessionId).catch((error) => {
      handleQueuedRequestFailure(message, error)
    })
  }
  const handleExternalEditor = async (): Promise<void> => {
    try {
      const r = await openInExternalEditor(input())
      setInput(r)
      queueMicrotask(autoResize)
    } catch (err) {
      console.warn('External editor:', err)
    }
  }
  const onTextareaInput = (v: string): void => {
    setInput(v)
    if (historyIndex() >= 0) setHistoryIndex(-1)
    autoResize()
    mention.checkMention(v, textareaRef?.selectionStart ?? v.length)
    slash.checkSlash(v, textareaRef?.selectionStart ?? v.length)
  }

  return {
    input,
    setInput,
    sendCount,
    elapsedSeconds: elapsedSecondsFromTimer,
    stashSize,
    attachments,
    isProcessing,
    inputDisabled,
    inputHasText,
    placeholder,
    escapeHint,
    mentionOpen: mention.mentionOpen,
    mentionFiltered: mention.mentionFiltered,
    mentionIndex: mention.mentionIndex,
    handleMentionSelect: (file: SearchableFile) =>
      mention.handleMentionSelect(file, input, setInput, textareaRef),
    slashOpen: slash.slashOpen,
    slashCommands: slash.slashCommands,
    slashIndex: slash.slashIndex,
    handleSlashSelect: (cmd: CommandEntry) =>
      slash.handleSlashSelect(cmd, input, setInput, textareaRef),
    handleSubmit,
    handleKeyDown,
    handleCancel,
    handleQueueFromMenu,
    handleInterruptFromMenu,
    handlePostCompleteFromMenu,
    handleExternalEditor,
    autoResize,
    onTextareaInput,
    setTextareaRef,
    focusTextarea,
    agent,
    sessionStore,
    settingsStore,
    ...modelState,
  }
}
