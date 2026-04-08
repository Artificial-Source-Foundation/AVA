/**
 * Message Input Component
 *
 * Chat input with Goose-style layout.
 */

import { type Component, createMemo, type JSX, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { DoomLoopBanner } from './DoomLoopBanner'
import { MessageInputShell } from './MessageInputShell'
import { FileMentionPopover } from './message-input/file-mention-popover'
import { InputDialogs } from './message-input/input-dialogs'
import type { QueuedItem } from './message-input/MessageQueueWidget'
import { SlashCommandPopover } from './message-input/slash-command-popover'
import { InputTextArea } from './message-input/text-area'
import { ToolbarStrip } from './message-input/toolbar-strip'
import { useInputState } from './message-input/use-input-state'

export interface MessageInputAdapter {
  doomBanner?: JSX.Element
  popovers: JSX.Element
  textarea: JSX.Element
  toolbar: JSX.Element
  dialogs?: JSX.Element
  shortcutHintSendCount: number
}

export const MessageInput: Component<{ adapter?: MessageInputAdapter }> = (props) => {
  const state = useInputState()
  const { openModelBrowser } = useLayout()

  const queuedItems = createMemo<QueuedItem[]>(() =>
    state.agent.messageQueue().map((msg, i) => ({
      id: `q-${i}-${msg.content.slice(0, 20)}`,
      content: msg.content,
      tier: (msg.tier as QueuedItem['tier']) ?? 'queued',
      group: msg.group,
    }))
  )

  const defaultAdapter = (): MessageInputAdapter => ({
    doomBanner: (
      <Show when={state.agent.doomLoopDetected()}>
        <DoomLoopBanner
          onStop={() => state.agent.cancel()}
          onRetry={() => state.agent.cancel()}
          onSwitchModel={() => openModelBrowser()}
        />
      </Show>
    ),
    popovers: (
      <div class="relative">
        <FileMentionPopover
          open={state.mentionOpen}
          files={state.mentionFiltered}
          onSelect={state.handleMentionSelect}
          selectedIndex={state.mentionIndex}
        />
        <SlashCommandPopover
          open={state.slashOpen}
          commands={state.slashCommands}
          onSelect={state.handleSlashSelect}
          selectedIndex={state.slashIndex}
        />
      </div>
    ),
    textarea: (
      <form onSubmit={state.handleSubmit}>
        <InputTextArea
          input={state.input}
          onInput={state.onTextareaInput}
          onKeyDown={state.handleKeyDown}
          onPaste={state.attachments.handlePaste}
          onDrop={state.attachments.handleDrop}
          isDragging={state.attachments.isDragging}
          setIsDragging={state.attachments.setIsDragging}
          disabled={state.inputDisabled}
          placeholder={state.placeholder}
          textareaRef={state.setTextareaRef}
          pendingImages={state.attachments.pendingImages}
          onRemoveImage={state.attachments.removeImage}
          pendingFiles={state.attachments.pendingFiles}
          onRemoveFile={state.attachments.removeFile}
          pendingPastes={state.attachments.pendingPastes}
          expandedPasteIndex={state.attachments.expandedPasteIndex}
          onTogglePastePreview={state.attachments.togglePastePreview}
          onRemovePaste={state.attachments.removePaste}
          onUpdatePaste={state.attachments.updatePaste}
          isProcessing={state.isProcessing}
          isStreaming={state.agent.isRunning}
          elapsedSeconds={state.elapsedSeconds}
          onCancel={state.handleCancel}
          inputHasText={state.inputHasText}
          queuedCount={state.agent.queuedCount}
          escapeHint={state.escapeHint}
          onQueue={state.handleQueueFromMenu}
          onInterrupt={state.handleInterruptFromMenu}
          onPostComplete={state.handlePostCompleteFromMenu}
          queuedMessages={queuedItems}
          onQueueRemove={(i) => state.agent.removeFromQueue(i)}
          onQueueReorder={(from, to) => state.agent.reorderInQueue(from, to)}
          onQueueEdit={(i, content) => state.agent.editInQueue(i, content)}
          onQueueClearAll={() => state.agent.clearQueue()}
        />
      </form>
    ),
    toolbar: (
      <ToolbarStrip
        currentModelDisplay={state.currentModelDisplay}
        modelSupportsReasoning={state.modelSupportsReasoning}
        handleCycleReasoning={state.handleCycleReasoning}
        isProcessing={state.isProcessing}
        agent={state.agent}
        sessionStore={state.sessionStore}
      />
    ),
    dialogs: (
      <InputDialogs
        input={state.input}
        setInput={state.setInput}
        autoResize={state.autoResize}
        enabledProviders={state.enabledProviders}
        focusTextarea={state.focusTextarea}
      />
    ),
    shortcutHintSendCount: state.sendCount(),
  })

  const adapter = () => props.adapter ?? defaultAdapter()

  return (
    <MessageInputShell
      doomBanner={adapter().doomBanner}
      popovers={adapter().popovers}
      textarea={adapter().textarea}
      toolbar={adapter().toolbar}
      dialogs={adapter().dialogs}
      shortcutHintSendCount={adapter().shortcutHintSendCount}
    />
  )
}
