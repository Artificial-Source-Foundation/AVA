/**
 * Attachment Previews
 *
 * Renders pending images, text-file chips, and collapsible paste blocks
 * above the textarea.
 */

import { ChevronDown, ChevronUp, Clipboard, FileText, X } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { getPastePreview } from './attachments'
import {
  PASTE_PREVIEW_LINES,
  type PendingFile,
  type PendingImage,
  type PendingPaste,
} from './types'

// ---------------------------------------------------------------------------
// Image previews
// ---------------------------------------------------------------------------

export interface ImagePreviewsProps {
  images: PendingImage[]
  onRemove: (index: number) => void
}

export const ImagePreviews: Component<ImagePreviewsProps> = (props) => (
  <Show when={props.images.length > 0}>
    <div class="flex gap-2 mb-2 flex-wrap px-3">
      <For each={props.images}>
        {(img, i) => (
          <div class="relative w-14 h-14 rounded overflow-hidden border border-[var(--border-subtle)]">
            <img
              src={`data:${img.mimeType};base64,${img.data}`}
              alt={img.name || 'Preview'}
              class="w-full h-full object-cover"
            />
            <button
              type="button"
              onClick={() => props.onRemove(i())}
              class="absolute -top-1 -right-1 w-4 h-4 bg-[var(--error)] text-white rounded-full text-[10px] leading-none flex items-center justify-center"
            >
              <X class="w-2.5 h-2.5" />
            </button>
          </div>
        )}
      </For>
    </div>
  </Show>
)

// ---------------------------------------------------------------------------
// File chips
// ---------------------------------------------------------------------------

export interface FileChipsProps {
  files: PendingFile[]
  onRemove: (index: number) => void
}

export const FileChips: Component<FileChipsProps> = (props) => (
  <Show when={props.files.length > 0}>
    <div class="flex gap-1.5 mb-2 flex-wrap px-3">
      <For each={props.files}>
        {(file, i) => (
          <div class="flex items-center gap-1.5 px-2 py-1 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[11px] text-[var(--text-secondary)]">
            <FileText class="w-3 h-3 text-[var(--text-muted)]" />
            <span class="truncate max-w-[120px]">{file.name}</span>
            <button
              type="button"
              onClick={() => props.onRemove(i())}
              class="w-3.5 h-3.5 flex items-center justify-center rounded-full hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
            >
              <X class="w-2.5 h-2.5" />
            </button>
          </div>
        )}
      </For>
    </div>
  </Show>
)

// ---------------------------------------------------------------------------
// Paste chips (collapsible)
// ---------------------------------------------------------------------------

export interface PasteChipsProps {
  pastes: PendingPaste[]
  expandedIndex: number | null
  onTogglePreview: (index: number) => void
  onRemove: (index: number) => void
}

export const PasteChips: Component<PasteChipsProps> = (props) => (
  <Show when={props.pastes.length > 0}>
    <div class="flex flex-col gap-1.5 mb-2 px-3">
      <For each={props.pastes}>
        {(paste, i) => (
          <div>
            <div class="flex items-center gap-1.5 px-2 py-1 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[11px] text-[var(--text-secondary)] w-fit">
              <Clipboard class="w-3 h-3 text-[var(--text-muted)]" />
              <button
                type="button"
                onClick={() => props.onTogglePreview(i())}
                class="flex items-center gap-1 hover:text-[var(--accent)] transition-colors"
              >
                <span>
                  Pasted text · {paste.lineCount} line{paste.lineCount !== 1 ? 's' : ''}
                </span>
                <Show when={props.expandedIndex === i()} fallback={<ChevronDown class="w-3 h-3" />}>
                  <ChevronUp class="w-3 h-3" />
                </Show>
              </button>
              <button
                type="button"
                onClick={() => props.onRemove(i())}
                class="w-3.5 h-3.5 flex items-center justify-center rounded-full hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
              >
                <X class="w-2.5 h-2.5" />
              </button>
            </div>
            {/* Expandable preview */}
            <Show when={props.expandedIndex === i()}>
              <pre class="mt-1 ml-1 px-2 py-1.5 text-[10px] leading-tight bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] overflow-x-auto max-h-[120px] overflow-y-auto text-[var(--text-secondary)] font-[var(--font-ui-mono)]">
                {getPastePreview(paste.content)}
                <Show when={paste.lineCount > PASTE_PREVIEW_LINES}>
                  <span class="text-[var(--text-muted)]">
                    {'\n'}... {paste.lineCount - PASTE_PREVIEW_LINES} more lines
                  </span>
                </Show>
              </pre>
            </Show>
          </div>
        )}
      </For>
    </div>
  </Show>
)
