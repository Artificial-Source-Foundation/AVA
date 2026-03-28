import { Crown, Pause, Send, Square } from 'lucide-solid'
import { type Component, createMemo, createSignal, For } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { Message } from '../../../types'
import { MessageBubble } from '../../chat/MessageBubble'
import { ModelSelector } from '../../chat/message-input/model-selector'
import { InputTextArea } from '../../chat/message-input/text-area'
import { ShortcutHint } from '../../chat/ShortcutHint'

const HQ_SESSION_ID = 'hq-director'

const HqDirectorChat: Component = () => {
  const {
    agents,
    directorMessages,
    liveDirectorContent,
    liveDirectorThinking,
    liveDirectorThinkingSegments,
    liveDirectorToolCalls,
    liveDirectorStreaming,
    sendDirectorMessage,
  } = useHq()
  const [steerText, setSteerText] = createSignal('')
  const [sendCount, setSendCount] = createSignal(0)
  const [isDragging, setIsDragging] = createSignal(false)
  let textareaEl: HTMLTextAreaElement | undefined

  const directorModel = () => agents().find((agent) => agent.tier === 'director')?.model || 'Auto'
  const emptyImages = () => [] as never[]
  const emptyFiles = () => [] as never[]
  const emptyPastes = () => [] as never[]
  const noExpandedPaste = () => null

  const autoResize = (): void => {
    if (!textareaEl) return
    textareaEl.style.height = 'auto'
    textareaEl.style.height = `${Math.min(textareaEl.scrollHeight, 200)}px`
  }

  const mappedMessages = createMemo<Message[]>(() =>
    directorMessages().map((msg) => ({
      id: msg.id,
      sessionId: HQ_SESSION_ID,
      role: msg.role === 'user' ? 'user' : 'assistant',
      content: msg.content,
      createdAt: msg.timestamp,
      model: msg.role === 'user' ? undefined : directorModel(),
      metadata: {
        mode: 'HQ Director',
      },
    }))
  )

  const liveMessage = createMemo<Message | null>(() => {
    if (!liveDirectorStreaming()) return null
    return {
      id: 'hq-live-director',
      sessionId: HQ_SESSION_ID,
      role: 'assistant',
      content: liveDirectorContent(),
      createdAt: Date.now(),
      model: directorModel(),
      toolCalls: liveDirectorToolCalls(),
      metadata: {
        mode: 'HQ Director',
        thinking: liveDirectorThinking(),
        thinkingSegments: liveDirectorThinkingSegments(),
      },
    }
  })

  const displayedMessages = createMemo(() => {
    const base = mappedMessages()
    const live = liveMessage()
    return live ? [...base, live] : base
  })

  const handleSend = (): void => {
    const value = steerText().trim()
    if (!value) return
    setSteerText('')
    setSendCount((count) => count + 1)
    void sendDirectorMessage(value)
    queueMicrotask(autoResize)
  }

  const inputHasText = () => !!steerText().trim()
  const placeholder = () =>
    liveDirectorStreaming()
      ? 'Type a message... (Enter = queue, Shift+Enter = newline)'
      : 'Steer the Director...'

  return (
    <div class="hq-director-chat flex h-full flex-col bg-[var(--background)]">
      <div
        class="flex items-center justify-between shrink-0 px-6 h-14"
        style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
      >
        <div class="flex items-center gap-2.5">
          <Crown size={18} style={{ color: '#f59e0b' }} />
          <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
            Director ({directorModel()})
          </span>
          <span
            class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
            style={{ color: 'var(--success)', 'background-color': 'rgba(34,197,94,0.15)' }}
          >
            active
          </span>
        </div>
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            title="Pause"
          >
            <Pause size={14} style={{ color: 'var(--text-secondary)' }} />
          </button>
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            title="Stop"
          >
            <Square size={14} style={{ color: 'var(--error)' }} />
          </button>
        </div>
      </div>

      <div class="relative flex-1 min-h-0 overflow-y-auto px-12 py-7">
        <For each={displayedMessages()}>
          {(message, index) => {
            const isStreaming = () => message.id === 'hq-live-director' && liveDirectorStreaming()
            const isLastMessage = () => index() === displayedMessages().length - 1
            const previous = () => displayedMessages()[index() - 1]
            const isRoleSwitch = () => {
              const prev = previous()
              if (!prev) return undefined
              return prev.role !== message.role
            }

            return (
              <div classList={{ 'mt-[4px]': isRoleSwitch() === false, 'mt-3': !!isRoleSwitch() }}>
                <MessageBubble
                  message={message}
                  shouldAnimate={false}
                  isEditing={false}
                  isRetrying={false}
                  isStreaming={!!isStreaming()}
                  isLastMessage={isLastMessage()}
                  streamingToolCalls={isStreaming() ? liveDirectorToolCalls() : undefined}
                  streamingContent={isStreaming() ? liveDirectorContent : undefined}
                  streamingThinkingSegments={
                    isStreaming() ? liveDirectorThinkingSegments() : undefined
                  }
                  onStartEdit={() => {}}
                  onCancelEdit={() => {}}
                  onSaveEdit={async () => {}}
                  onRetry={() => {}}
                  onRegenerate={() => {}}
                  onCopy={() => {}}
                  onDelete={() => {}}
                  onBranch={() => {}}
                  onRewind={() => {}}
                />
              </div>
            )
          }}
        </For>
      </div>

      <div class="shrink-0 px-7 py-4 border-t border-[var(--gray-5)]">
        <div class="space-y-1.5">
          <InputTextArea
            input={steerText}
            onInput={(value) => {
              setSteerText(value)
              queueMicrotask(autoResize)
            }}
            onKeyDown={(event) => {
              if (event.key === 'Enter' && !event.shiftKey) {
                event.preventDefault()
                handleSend()
              }
            }}
            onPaste={() => {}}
            onDrop={(event) => event.preventDefault()}
            isDragging={isDragging}
            setIsDragging={setIsDragging}
            disabled={() => false}
            placeholder={placeholder}
            textareaRef={(el) => {
              textareaEl = el
              autoResize()
            }}
            pendingImages={emptyImages}
            onRemoveImage={() => {}}
            pendingFiles={emptyFiles}
            onRemoveFile={() => {}}
            pendingPastes={emptyPastes}
            expandedPasteIndex={noExpandedPaste}
            onTogglePastePreview={() => {}}
            onRemovePaste={() => {}}
            isProcessing={liveDirectorStreaming}
            isStreaming={liveDirectorStreaming}
            elapsedSeconds={() => 0}
            onCancel={() => {}}
            inputHasText={inputHasText}
          />
          <div class="flex items-center justify-between gap-2 text-[var(--text-xs)] text-[var(--text-tertiary)] font-[var(--font-ui-mono)] select-none overflow-x-auto flex-wrap min-w-0">
            <div class="flex items-center gap-2 flex-wrap min-w-0">
              <ModelSelector onToggle={() => {}} currentModelDisplay={directorModel} />
              <span class="w-px h-4 bg-[var(--border-subtle)] shrink-0" />
              <span class="inline-flex items-center gap-1 rounded-full px-2.5 py-1 bg-[var(--alpha-white-5)] text-[var(--text-secondary)]">
                <Crown size={12} style={{ color: '#f59e0b' }} />
                Director
              </span>
              <span class="inline-flex items-center gap-1 rounded-full px-2.5 py-1 bg-[var(--alpha-white-5)] text-[var(--text-secondary)]">
                {liveDirectorStreaming() ? 'Streaming' : 'Ready'}
              </span>
            </div>
            <button
              type="button"
              class="inline-flex items-center justify-center w-8 h-8 rounded-[var(--radius-lg)] bg-[var(--accent)]"
              onClick={handleSend}
              disabled={!inputHasText()}
              style={{ opacity: inputHasText() ? '1' : '0.45' }}
              title="Send Director message"
            >
              <Send size={15} style={{ color: 'white' }} />
            </button>
          </div>
          <ShortcutHint sendCount={sendCount()} />
        </div>
      </div>
    </div>
  )
}

export default HqDirectorChat
