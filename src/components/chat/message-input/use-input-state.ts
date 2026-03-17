import { type Accessor, createEffect, createMemo, createSignal, on, onCleanup } from 'solid-js'
import { useAgent } from '../../../hooks/useAgent'
import { useChat } from '../../../hooks/useChat'
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
  mentionOpen: Accessor<boolean>
  mentionFiltered: Accessor<SearchableFile[]>
  mentionIndex: Accessor<number>
  handleMentionSelect: (file: SearchableFile) => void
  handleSubmit: (e: Event) => Promise<void>
  handleKeyDown: (e: KeyboardEvent) => void
  handleCancel: () => void
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
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)
  const [historyIndex, setHistoryIndex] = createSignal(-1)
  const [savedDraft, setSavedDraft] = createSignal('')
  const [stashSize, setStashSize] = createSignal(getStash().length)
  let submitting = false
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

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
  // users to type while the agent runs (Enter = steer, Alt+Enter = follow-up)
  const inputDisabled = createMemo(() => false)
  const inputHasText = createMemo(() => !!input().trim())
  const placeholder = createMemo(() =>
    isProcessing()
      ? 'Steer the agent... (Enter = steer, Alt+Enter = follow-up)'
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
      const h = `${Math.min(textareaRef.scrollHeight, 200)}px`
      if (textareaRef.style.height !== h) textareaRef.style.height = h
      resizeFrame = undefined
    })
  }

  // Elapsed timer during streaming
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
    window.removeEventListener('ava:set-input', handleExternalInput)
    window.removeEventListener('ava:stash-prompt', handleStash)
    window.removeEventListener('ava:restore-prompt', handleRestore)
  })

  const handleSubmit = async (e: Event): Promise<void> => {
    e.preventDefault()
    const message = input().trim()
    if (!message || submitting) return

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
          id: `sys-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
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

    // Mid-stream messaging: route to steer/follow-up when agent is running
    if (isProcessing()) {
      setInput('')
      setHistoryIndex(-1)
      if (textareaRef) textareaRef.style.height = 'auto'
      // Default: steering (Enter). Follow-up handled in handleKeyDown with Alt+Enter.
      agent.steer(message)
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
    // Mid-stream messaging keybinds (while agent is running)
    if (isProcessing() && e.key === 'Enter') {
      const message = input().trim()
      if (!message) return

      if (e.ctrlKey && e.altKey) {
        // Ctrl+Alt+Enter: post-complete (Tier 3)
        e.preventDefault()
        setInput('')
        if (textareaRef) textareaRef.style.height = 'auto'
        agent.postComplete(message)
        return
      }
      if (e.altKey) {
        // Alt+Enter: follow-up (Tier 2)
        e.preventDefault()
        setInput('')
        if (textareaRef) textareaRef.style.height = 'auto'
        agent.followUp(message)
        return
      }
      if (!e.shiftKey) {
        // Enter: steer (Tier 1)
        e.preventDefault()
        void handleSubmit(e)
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
    elapsedSeconds,
    stashSize,
    attachments,
    isProcessing,
    inputDisabled,
    inputHasText,
    placeholder,
    mentionOpen: mention.mentionOpen,
    mentionFiltered: mention.mentionFiltered,
    mentionIndex: mention.mentionIndex,
    handleMentionSelect: (file: SearchableFile) =>
      mention.handleMentionSelect(file, input, setInput, textareaRef),
    handleSubmit,
    handleKeyDown,
    handleCancel,
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
