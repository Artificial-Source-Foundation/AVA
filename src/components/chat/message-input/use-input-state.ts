import { type Accessor, createEffect, createMemo, createSignal, on, onCleanup } from 'solid-js'
import { useAgent } from '../../../hooks/useAgent'
import { useChat } from '../../../hooks/useChat'
import { useElapsedTimer } from '../../../hooks/useElapsedTimer'
import { generateMessageId } from '../../../lib/ids'
import { parseSlashCommand } from '../../../services/command-resolver'
import type { SearchableFile } from '../../../services/file-search'
import { openInExternalEditor } from '../../../services/ide-integration'
import { getStash, popStash, pushStash } from '../../../services/prompt-stash'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { createAttachmentState } from './attachment-bar'
import { buildFullMessage } from './attachments'
import { useMentionState } from './use-mention-state'
import { type ModelState, useModelState } from './use-model-state'
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
  chat: ReturnType<typeof useChat>
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
  const chat = useChat()
  const agent = useAgent()
  const sessionStore = useSession()
  const { selectedModel, messages } = sessionStore
  const settingsStore = useSettings()
  const { settings } = settingsStore
  const modelState = useModelState()
  const mention = useMentionState()
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
  const isProcessing = createMemo(() => chat.isStreaming() || agent.isRunning())
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
      ? 'Type a message... (Enter = queue, Ctrl+Enter = interrupt)'
      : agent.isPlanMode()
        ? 'Plan your approach... (Ctrl+/ for commands)'
        : 'Ask anything... (Ctrl+/ for commands)'
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

  // Elapsed timer during streaming
  const elapsedSecondsFromTimer = useElapsedTimer(() => chat.streamingStartedAt())

  // Clear queue on session change
  createEffect(
    on(
      () => sessionStore.currentSession()?.id,
      () => chat.clearQueue()
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

    // Handle /later and /queue slash commands locally
    const parsed = parseSlashCommand(message)
    if (parsed) {
      if (parsed.name === 'later') {
        const laterMsg = parsed.args.trim()
        if (laterMsg) {
          agent.postComplete(laterMsg)
        }
        setInput('')
        setHistoryIndex(-1)
        if (textareaRef) textareaRef.style.height = 'auto'
        return
      }
      if (parsed.name === 'queue') {
        const queue = chat.messageQueue()
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
    }

    // Mid-stream messaging: queue for next turn when agent is running (Enter)
    if (isProcessing()) {
      setInput('')
      setHistoryIndex(-1)
      if (textareaRef) textareaRef.style.height = 'auto'
      // Add the queued message to chat so the user sees what they sent
      const sessionId = sessionStore.currentSession()?.id ?? ''
      sessionStore.addMessage({
        id: generateMessageId('queue'),
        sessionId,
        role: 'user',
        content: message,
        createdAt: Date.now(),
        metadata: { tier: 'queued' },
      })
      // Queue: agent finishes current turn, then processes message as new turn
      agent.followUp(message)
      return
    }

    submitting = true
    setSendCount((c) => c + 1)
    setInput('')
    setHistoryIndex(-1)
    if (textareaRef) textareaRef.style.height = 'auto'
    chat.clearError()
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
        setInput('')
        if (textareaRef) textareaRef.style.height = 'auto'
        const sessionId = sessionStore.currentSession()?.id ?? ''
        sessionStore.addMessage({
          id: generateMessageId('post'),
          sessionId,
          role: 'user',
          content: message,
          createdAt: Date.now(),
          metadata: { tier: 'post-complete' },
        })
        agent.postComplete(message)
        return
      }
      if (!e.shiftKey) {
        // Enter (no modifiers): queue — agent finishes current turn, then processes
        e.preventDefault()
        setInput('')
        if (textareaRef) textareaRef.style.height = 'auto'
        const sessionId = sessionStore.currentSession()?.id ?? ''
        sessionStore.addMessage({
          id: generateMessageId('queue'),
          sessionId,
          role: 'user',
          content: message,
          createdAt: Date.now(),
          metadata: { tier: 'queued' },
        })
        agent.followUp(message)
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
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    const sessionId = sessionStore.currentSession()?.id ?? ''
    sessionStore.addMessage({
      id: generateMessageId('queue'),
      sessionId,
      role: 'user',
      content: message,
      createdAt: Date.now(),
      metadata: { tier: 'queued' },
    })
    agent.followUp(message)
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
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    const sessionId = sessionStore.currentSession()?.id ?? ''
    sessionStore.addMessage({
      id: generateMessageId('post'),
      sessionId,
      role: 'user',
      content: message,
      createdAt: Date.now(),
      metadata: { tier: 'post-complete' },
    })
    agent.postComplete(message)
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
    chat,
    agent,
    sessionStore,
    settingsStore,
    ...modelState,
  }
}
