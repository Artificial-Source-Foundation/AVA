/**
 * Message Input Component
 *
 * Chat input with Goose-style layout:
 * - Send/cancel buttons inside the textarea
 * - Single unified strip below with model selector, toggles, and context info
 *
 * Sub-components live in ./message-input/ for modularity.
 */

import { type Component, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { DoomLoopBanner } from './DoomLoopBanner'
import { FileMentionPopover } from './message-input/file-mention-popover'
import { InputDialogs } from './message-input/input-dialogs'
import { InputTextArea } from './message-input/text-area'
import { ToolbarStrip } from './message-input/toolbar-strip'
import { useInputState } from './message-input/use-input-state'
import { ShortcutHint } from './ShortcutHint'

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const MessageInput: Component = () => {
  const state = useInputState()
  const { openModelBrowser } = useLayout()

  return (
    <div class="px-7 py-4 border-t border-[var(--gray-5)]">
      <Show when={state.agent.doomLoopDetected()}>
        <DoomLoopBanner
          onStop={() => state.agent.cancel()}
          onRetry={() => state.agent.cancel()}
          onSwitchModel={() => openModelBrowser()}
        />
      </Show>
      <form onSubmit={state.handleSubmit} class="space-y-1.5">
        {/* @ mention autocomplete popover */}
        <div class="relative">
          <FileMentionPopover
            open={state.mentionOpen}
            files={state.mentionFiltered}
            onSelect={state.handleMentionSelect}
            selectedIndex={state.mentionIndex}
          />
        </div>
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
          isStreaming={state.chat.isStreaming}
          elapsedSeconds={state.elapsedSeconds}
          onCancel={state.handleCancel}
          inputHasText={state.inputHasText}
          queuedCount={state.agent.queuedCount}
        />
        <ShortcutHint sendCount={state.sendCount()} />

        <ToolbarStrip
          currentModelDisplay={state.currentModelDisplay}
          modelSupportsReasoning={state.modelSupportsReasoning}
          handleCycleReasoning={state.handleCycleReasoning}
          toggleDelegation={state.toggleDelegation}
          isProcessing={state.isProcessing}
          stashSize={state.stashSize}
          chat={state.chat}
          agent={state.agent}
          sessionStore={state.sessionStore}
        />
      </form>
      <InputDialogs
        input={state.input}
        setInput={state.setInput}
        autoResize={state.autoResize}
        enabledProviders={state.enabledProviders}
        focusTextarea={state.focusTextarea}
      />
    </div>
  )
}
