/**
 * Input Text Area
 *
 * Drop-zone wrapper around the textarea with attachment previews,
 * drag overlay, auto-resize, paste/drop handling, and keyboard shortcuts.
 * Send/cancel buttons and streaming stats are rendered inside the textarea.
 */

import { ArrowUp, Pause, Play, Square } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import { FileChips, ImagePreviews, PasteChips } from './attachment-previews'
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
  // Send / cancel / streaming
  isProcessing: Accessor<boolean>
  isStreaming: Accessor<boolean>
  elapsedSeconds: Accessor<number>
  onCancel: () => void
  inputHasText: Accessor<boolean>
  // Pause / resume
  isPaused?: Accessor<boolean>
  onPause?: () => void
  onResume?: () => void
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
    />

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
        w-full density-section-px density-section-py pr-[110px]
        bg-[var(--input-background)] text-[var(--text-primary)]
        placeholder-[var(--input-placeholder)]
        border border-[var(--input-border)] rounded-lg
        resize-none transition-colors
        focus:outline-none focus:border-[var(--input-border-focus)]
        disabled:opacity-50
      "
      style={{ 'min-height': '44px', 'max-height': '200px', 'font-size': 'var(--chat-font-size)' }}
    />

    {/* Send / Cancel / Streaming — inside textarea, vertically centered right */}
    <div class="absolute right-2 top-0 bottom-0 flex items-center gap-1.5">
      {/* Streaming elapsed time */}
      <Show when={props.isStreaming()}>
        <span class="flex items-center gap-1 text-[10px] text-[var(--text-tertiary)] tabular-nums">
          <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
          {props.elapsedSeconds()}s
        </span>
      </Show>

      {/* Pause / Resume button */}
      <Show when={props.isProcessing() && props.onPause}>
        <Show
          when={props.isPaused?.()}
          fallback={
            <button
              type="button"
              onClick={props.onPause}
              class="p-1.5 bg-[var(--warning)] hover:brightness-110 text-white rounded-[var(--radius-md)] transition-colors"
              title="Pause agent"
            >
              <Pause class="w-3.5 h-3.5" />
            </button>
          }
        >
          <button
            type="button"
            onClick={props.onResume}
            class="p-1.5 bg-[var(--success)] hover:brightness-110 text-white rounded-[var(--radius-md)] transition-colors"
            title="Resume agent"
          >
            <Play class="w-3.5 h-3.5" />
          </button>
        </Show>
      </Show>

      {/* Cancel button */}
      <Show when={props.isProcessing()}>
        <button
          type="button"
          onClick={props.onCancel}
          class="p-1.5 bg-[var(--error)] hover:brightness-110 text-white rounded-[var(--radius-md)] transition-colors"
          title="Cancel"
        >
          <Square class="w-3.5 h-3.5" />
        </button>
      </Show>

      {/* Send button — always accent-colored */}
      <button
        type="submit"
        disabled={!props.inputHasText() || props.isProcessing()}
        class="
          p-1.5 rounded-[var(--radius-md)] transition-colors
          disabled:opacity-30 disabled:cursor-not-allowed
          bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white
        "
        title="Send message"
      >
        <ArrowUp class="w-3.5 h-3.5" />
      </button>
    </div>
  </div>
)
