/**
 * Input Text Area
 *
 * Drop-zone wrapper around the textarea with attachment previews,
 * drag overlay, auto-resize, paste/drop handling, and keyboard shortcuts.
 * Send/cancel buttons and streaming stats are rendered inside the textarea.
 */

import { type Accessor, type Component, Show } from 'solid-js'
import { FileChips, ImagePreviews, PasteChips } from './attachment-previews'
import { SubmitButton } from './submit-button'
import type { PendingFile, PendingImage, PendingPaste } from './types'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface InputTextAreaProps {
  input: Accessor<string>
  onInput: (value: string) => void
  onKeyDown: (e: KeyboardEvent) => void
  onPaste: (e: ClipboardEvent) => void
  onDrop: (e: DragEvent) => void
  isDragging: Accessor<boolean>
  setIsDragging: (v: boolean) => void
  disabled: Accessor<boolean>
  placeholder: Accessor<string>
  textareaRef: (el: HTMLTextAreaElement) => void
  // Attachment state
  pendingImages: Accessor<PendingImage[]>
  onRemoveImage: (index: number) => void
  pendingFiles: Accessor<PendingFile[]>
  onRemoveFile: (index: number) => void
  pendingPastes: Accessor<PendingPaste[]>
  expandedPasteIndex: Accessor<number | null>
  onTogglePastePreview: (index: number) => void
  onRemovePaste: (index: number) => void
  onUpdatePaste?: (index: number, content: string) => void
  // Send / cancel / streaming
  isProcessing: Accessor<boolean>
  isStreaming: Accessor<boolean>
  elapsedSeconds: Accessor<number>
  onCancel: () => void
  inputHasText: Accessor<boolean>
  // Mid-stream messaging
  queuedCount?: Accessor<number>
  escapeHint?: Accessor<boolean>
  onSteer?: () => void
  onFollowUp?: () => void
  onPostComplete?: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const InputTextArea: Component<InputTextAreaProps> = (props) => (
  // biome-ignore lint/a11y/noStaticElementInteractions: drop zone for images and files
  <div
    class="relative"
    onDrop={props.onDrop}
    onDragOver={(e: DragEvent) => {
      e.preventDefault()
      props.setIsDragging(true)
    }}
    onDragLeave={() => props.setIsDragging(false)}
  >
    {/* Drag overlay */}
    <Show when={props.isDragging()}>
      <div class="absolute inset-0 z-10 flex items-center justify-center rounded-lg border-2 border-dashed border-[var(--accent)] bg-[var(--accent-subtle)]">
        <span class="text-xs font-medium text-[var(--accent)]">Drop files here</span>
      </div>
    </Show>

    <ImagePreviews images={props.pendingImages()} onRemove={props.onRemoveImage} />
    <FileChips files={props.pendingFiles()} onRemove={props.onRemoveFile} />
    <PasteChips
      pastes={props.pendingPastes()}
      expandedIndex={props.expandedPasteIndex()}
      onTogglePreview={props.onTogglePastePreview}
      onRemove={props.onRemovePaste}
      onUpdatePaste={props.onUpdatePaste}
    />

    {/* Textarea + submit button in own relative container so button stays anchored */}
    <div class="relative">
      <textarea
        ref={props.textareaRef}
        value={props.input()}
        onInput={(e) => props.onInput(e.currentTarget.value)}
        onKeyDown={props.onKeyDown}
        onPaste={props.onPaste}
        placeholder={props.placeholder()}
        disabled={props.disabled()}
        rows={1}
        class="
          message-composer-textarea
          w-full px-[18px] py-3.5 pr-[110px]
          bg-[var(--gray-2)] text-[var(--text-primary)]
          placeholder-[var(--gray-7)]
          border border-[var(--gray-5)] rounded-[var(--radius-2xl)]
          resize-none transition-colors
          focus:outline-none focus:border-[var(--accent)]
          disabled:opacity-50
        "
        style={{
          'min-height': '44px',
          'max-height': '200px',
          'font-size': 'var(--chat-font-size)',
          transition: 'height 100ms ease, border-color 150ms ease',
        }}
      />

      {/* Send / Cancel / Streaming — inside textarea, vertically centered right */}
      <SubmitButton
        isProcessing={props.isProcessing}
        isStreaming={props.isStreaming}
        elapsedSeconds={props.elapsedSeconds}
        onCancel={props.onCancel}
        inputHasText={props.inputHasText}
        queuedCount={props.queuedCount}
        escapeHint={props.escapeHint}
        onSteer={props.onSteer}
        onFollowUp={props.onFollowUp}
        onPostComplete={props.onPostComplete}
      />
    </div>
  </div>
)
