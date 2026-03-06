/**
 * useInputState Hook
 *
 * Encapsulates all input-related signals, effects, and event handlers for
 * the MessageInput component. Keeps the composition shell thin.
 */

import { type Accessor, createEffect, createMemo, createSignal, on, onCleanup } from 'solid-js'
import { useAgent } from '../../../hooks/useAgent'
import { useChat } from '../../../hooks/useChat'
import type { SearchableFile } from '../../../services/file-search'
import { filterFiles, getProjectFiles } from '../../../services/file-search'
import { openInExternalEditor } from '../../../services/ide-integration'
import { getStash, popStash, pushStash } from '../../../services/prompt-stash'
import { useProject } from '../../../stores/project'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { createAttachmentState } from './attachment-bar'
import { buildFullMessage } from './attachments'
import { cycleReasoningEffort } from './toolbar-buttons'

// ---------------------------------------------------------------------------
// Return type
// ---------------------------------------------------------------------------

export interface InputState {
  // Signals
  input: Accessor<string>
  setInput: (v: string | ((prev: string) => string)) => void
  sendCount: Accessor<number>
  elapsedSeconds: Accessor<number>
  stashSize: Accessor<number>
  attachments: ReturnType<typeof createAttachmentState>

  // Derived
  isProcessing: Accessor<boolean>
  inputDisabled: Accessor<boolean>
  inputHasText: Accessor<boolean>
  placeholder: Accessor<string>
  enabledProviders: Accessor<ReturnType<ReturnType<typeof useSettings>['settings']>['providers']>

  // Model / reasoning derived
  currentModelDisplay: Accessor<string>
  activeProviderId: Accessor<string | null>
  modelSupportsReasoning: Accessor<boolean>

  // @ mention
  mentionOpen: Accessor<boolean>
  mentionFiltered: Accessor<SearchableFile[]>
  mentionIndex: Accessor<number>
  handleMentionSelect: (file: SearchableFile) => void

  // Event handlers
  handleSubmit: (e: Event) => Promise<void>
  handleKeyDown: (e: KeyboardEvent) => void
  handleCancel: () => void
  handleCycleReasoning: () => void
  toggleDelegation: () => void
  handleExternalEditor: () => Promise<void>
  autoResize: () => void

  // Refs
  setTextareaRef: (el: HTMLTextAreaElement) => void
  getTextareaRef: () => HTMLTextAreaElement | undefined

  // Store accessors (pass-through for toolbar/dialogs)
  chat: ReturnType<typeof useChat>
  agent: ReturnType<typeof useAgent>
  sessionStore: ReturnType<typeof useSession>
  settings: ReturnType<typeof useSettings>
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function useInputState(): InputState {
  // Signals
  const [input, setInputRaw] = createSignal('')
  const [sendCount, setSendCount] = createSignal(0)
  const [elapsedSeconds, setElapsedSeconds] = createSignal(0)
  let submitting = false
  let textareaRef: HTMLTextAreaElement | undefined
  let resizeFrame: number | undefined

  // Prompt history
  const [historyIndex, setHistoryIndex] = createSignal(-1)
  const [savedDraft, setSavedDraft] = createSignal('')

  // Stash
  const [stashSize, setStashSize] = createSignal(getStash().length)

  // Attachments
  const attachments = createAttachmentState()

  // Hooks / stores
  const chat = useChat()
  const agent = useAgent()
  const sessionStore = useSession()
  const { currentProject } = useProject()
  const { selectedModel, selectedProvider, setSelectedModel, messages } = sessionStore
  const settingsStore = useSettings()
  const { settings, updateSettings } = settingsStore

  // Ref accessors
  const setTextareaRef = (el: HTMLTextAreaElement): void => {
    textareaRef = el
  }
  const getTextareaRef = (): HTMLTextAreaElement | undefined => textareaRef

  // Prompt history
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

  // Preload project files
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

  // ── Derived state ──

  const isProcessing = () => chat.isStreaming() || agent.isRunning()
  const inputDisabled = () => isProcessing()
  const inputHasText = createMemo(() => !!input().trim())

  const enabledProviders = createMemo(() =>
    settings().providers.filter((p) => p.enabled && p.models.length > 0)
  )

  // Auto-select valid model
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

  const placeholder = () =>
    isProcessing()
      ? `Working... (turn ${agent.currentTurn()})`
      : agent.isPlanMode()
        ? 'Plan your approach...'
        : 'Ask anything...'

  // ── Auto-resize ──

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

  // ── Effects ──

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

  // ── Stash events ──

  const handleStash = (): void => {
    const text = input().trim()
    if (!text) return
    pushStash(text)
    setInputRaw('')
    setStashSize(getStash().length)
    queueMicrotask(autoResize)
  }
  const handleRestore = (): void => {
    const text = popStash()
    if (text) {
      setInputRaw(text)
      setStashSize(getStash().length)
      queueMicrotask(autoResize)
    }
  }
  window.addEventListener('ava:stash-prompt', handleStash)
  window.addEventListener('ava:restore-prompt', handleRestore)

  // External input
  const handleExternalInput = (e: Event): void => {
    const text = (e as CustomEvent<{ text: string }>).detail.text
    setInputRaw(text)
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

  // ── @ mention helpers ──

  const checkMention = (value: string, cursorPos: number): void => {
    let atPos = -1
    for (let i = cursorPos - 1; i >= 0; i--) {
      const ch = value[i]
      if (ch === '@') {
        if (i === 0 || /\s/.test(value[i - 1])) atPos = i
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

  const handleMentionSelect = (file: SearchableFile): void => {
    const start = mentionStart()
    if (start < 0) return
    const value = input()
    const before = value.slice(0, start)
    const cursorEnd = start + 1 + mentionQuery().length
    const after = value.slice(cursorEnd)
    const inserted = `@${file.relative} `
    setInputRaw(before + inserted + after)
    setMentionOpen(false)
    textareaRef?.focus()
    const newPos = before.length + inserted.length
    queueMicrotask(() => {
      textareaRef?.setSelectionRange(newPos, newPos)
    })
  }

  // ── onInput handler (used by composition shell) ──
  // Note: the composition shell calls this via the onInput prop.

  // ── Submit ──

  const handleSubmit = async (e: Event): Promise<void> => {
    e.preventDefault()
    const message = input().trim()
    if (!message || submitting) return
    if (isProcessing()) return
    submitting = true
    setSendCount((c) => c + 1)
    setInputRaw('')
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

  // ── Keydown ──

  const handleKeyDown = (e: KeyboardEvent): void => {
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
        setInputRaw(history[0])
        return
      }
      if (historyIndex() >= 0 && historyIndex() < history.length - 1) {
        e.preventDefault()
        const newIdx = historyIndex() + 1
        setHistoryIndex(newIdx)
        setInputRaw(history[newIdx])
        return
      }
    }
    if (e.key === 'ArrowDown' && historyIndex() >= 0 && !mentionOpen()) {
      e.preventDefault()
      if (historyIndex() === 0) {
        setHistoryIndex(-1)
        setInputRaw(savedDraft())
      } else {
        const newIdx = historyIndex() - 1
        setHistoryIndex(newIdx)
        setInputRaw(promptHistory()[newIdx])
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

  // ── Settings toggles ──

  const handleCycleReasoning = (): void => {
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

  const toggleDelegation = (): void => {
    updateSettings({
      generation: {
        ...settings().generation,
        delegationEnabled: !settings().generation.delegationEnabled,
      },
    })
  }

  const handleCancel = (): void => {
    agent.cancel()
  }

  const handleExternalEditor = async (): Promise<void> => {
    try {
      const result = await openInExternalEditor(input())
      setInputRaw(result)
      queueMicrotask(autoResize)
    } catch (err) {
      console.warn('External editor:', err)
    }
  }

  // Provide a way for the textarea onInput to reset history + trigger mention check
  const onTextareaInput = (v: string): void => {
    setInputRaw(v)
    if (historyIndex() >= 0) setHistoryIndex(-1)
    autoResize()
    const cursor = textareaRef?.selectionStart ?? v.length
    checkMention(v, cursor)
  }

  return {
    input,
    setInput: setInputRaw,
    sendCount,
    elapsedSeconds,
    stashSize,
    attachments,

    isProcessing,
    inputDisabled,
    inputHasText,
    placeholder,
    enabledProviders,

    currentModelDisplay,
    activeProviderId,
    modelSupportsReasoning,

    mentionOpen,
    mentionFiltered,
    mentionIndex,
    handleMentionSelect,

    handleSubmit,
    handleKeyDown,
    handleCancel,
    handleCycleReasoning,
    toggleDelegation,
    handleExternalEditor,
    autoResize,

    setTextareaRef,
    getTextareaRef,

    chat,
    agent,
    sessionStore,
    settings: settingsStore,

    // Expose onTextareaInput as a convenience; the composition shell
    // can call it from the InputTextArea's onInput prop.
    onTextareaInput,
  } as InputState & { onTextareaInput: (v: string) => void }
}
