/**
 * Attachment Previews
 *
 * Renders pending images, text-file chips, and collapsible paste blocks
 * above the textarea. Paste chips open a modal for viewing/editing.
 * Image thumbnails open a preview modal on click.
 */

import { Clipboard, FileText, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { PendingFile, PendingImage, PendingPaste } from './types'

// ---------------------------------------------------------------------------
// Image preview modal
// ---------------------------------------------------------------------------

const [imageModalSrc, setImageModalSrc] = createSignal<string | null>(null)

const ImageModal: Component = () => (
  <Show when={imageModalSrc()}>
    <div
      class="fixed inset-0 z-50 flex items-center justify-center bg-black/70"
      onClick={() => setImageModalSrc(null)}
      onKeyDown={(e) => {
        if (e.key === 'Escape') setImageModalSrc(null)
      }}
      role="dialog"
    >
      {/* biome-ignore lint/a11y/noStaticElementInteractions: modal stop propagation */}
      {/* biome-ignore lint/a11y/useKeyWithClickEvents: handled on parent */}
      <div
        class="relative max-w-[95vw] max-h-[95vh] rounded-lg overflow-hidden"
        onClick={(e) => e.stopPropagation()}
      >
        <img src={imageModalSrc()!} alt="Preview" class="max-w-full max-h-[92vh] object-contain" />
        <button
          type="button"
          onClick={() => setImageModalSrc(null)}
          class="absolute top-2 right-2 w-7 h-7 bg-black/60 hover:bg-black/80 text-white rounded-full flex items-center justify-center transition-colors"
        >
          <X class="w-4 h-4" />
        </button>
      </div>
    </div>
  </Show>
)

// ---------------------------------------------------------------------------
// Paste edit modal
// ---------------------------------------------------------------------------

const [pasteModalData, setPasteModalData] = createSignal<{
  content: string
  index: number
  onSave: (index: number, content: string) => void
} | null>(null)

const PasteModal: Component = () => {
  let textareaRef: HTMLTextAreaElement | undefined

  const data = () => pasteModalData()

  const handleSave = () => {
    const d = data()
    if (!d || !textareaRef) return
    d.onSave(d.index, textareaRef.value)
    setPasteModalData(null)
  }

  return (
    <Show when={data()}>
      <div
        class="fixed inset-0 z-50 flex items-center justify-center bg-black/70"
        onClick={() => setPasteModalData(null)}
        onKeyDown={(e) => {
          if (e.key === 'Escape') setPasteModalData(null)
        }}
        role="dialog"
      >
        {/* biome-ignore lint/a11y/noStaticElementInteractions: modal stop propagation */}
        {/* biome-ignore lint/a11y/useKeyWithClickEvents: handled on parent */}
        <div
          class="w-[min(95vw,900px)] h-[min(90vh,800px)] flex flex-col rounded-lg bg-[var(--surface-overlay)] border border-[var(--border-default)] shadow-2xl"
          onClick={(e) => e.stopPropagation()}
        >
          <div class="flex items-center justify-between px-4 py-3 border-b border-[var(--border-subtle)]">
            <span class="text-[var(--text-base)] font-medium text-[var(--text-primary)]">
              Edit pasted text ({data()!.content.split('\n').length} lines)
            </span>
            <div class="flex items-center gap-2">
              <button
                type="button"
                onClick={() => setPasteModalData(null)}
                class="px-3 py-1 text-[var(--text-sm)] rounded-[var(--radius-md)] text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={handleSave}
                class="px-3 py-1 text-[var(--text-sm)] rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:opacity-90 transition-opacity"
              >
                Save
              </button>
            </div>
          </div>
          <textarea
            ref={textareaRef}
            class="flex-1 w-full p-4 bg-transparent text-[var(--text-base)] leading-relaxed text-[var(--text-primary)] font-[var(--font-ui-mono)] resize-none outline-none scrollbar-thin"
            spellcheck={false}
          >
            {data()!.content}
          </textarea>
        </div>
      </div>
    </Show>
  )
}

// ---------------------------------------------------------------------------
// Image previews
// ---------------------------------------------------------------------------

export interface ImagePreviewsProps {
  images: PendingImage[]
  onRemove: (index: number) => void
}

export const ImagePreviews: Component<ImagePreviewsProps> = (props) => (
  <>
    <ImageModal />
    <Show when={props.images.length > 0}>
      <div class="flex gap-2 mb-2 flex-wrap px-3">
        <For each={props.images}>
          {(img, i) => {
            const src = () => `data:${img.mimeType};base64,${img.data}`
            return (
              <div class="relative w-14 h-14 rounded overflow-hidden border border-[var(--border-subtle)] cursor-pointer group">
                <button
                  type="button"
                  class="w-full h-full p-0 border-0 bg-transparent"
                  onClick={() => setImageModalSrc(src())}
                >
                  <img
                    src={src()}
                    alt={img.name || 'Preview'}
                    class="w-full h-full object-cover transition-opacity group-hover:opacity-80"
                  />
                </button>
                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation()
                    props.onRemove(i())
                  }}
                  class="absolute -top-1 -right-1 w-4 h-4 bg-[var(--error)] text-white rounded-full text-[var(--text-2xs)] leading-none flex items-center justify-center"
                >
                  <X class="w-2.5 h-2.5" />
                </button>
              </div>
            )
          }}
        </For>
      </div>
    </Show>
  </>
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
          <div class="flex items-center gap-1.5 px-2 py-1 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-xs)] text-[var(--text-secondary)]">
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
// Paste chips — click opens edit modal
// ---------------------------------------------------------------------------

export interface PasteChipsProps {
  pastes: PendingPaste[]
  expandedIndex: number | null
  onTogglePreview: (index: number) => void
  onRemove: (index: number) => void
  onUpdatePaste?: (index: number, content: string) => void
}

export const PasteChips: Component<PasteChipsProps> = (props) => (
  <>
    <PasteModal />
    <Show when={props.pastes.length > 0}>
      <div class="flex gap-1.5 mb-2 flex-wrap px-3">
        <For each={props.pastes}>
          {(paste, i) => (
            <div class="flex items-center gap-1.5 px-2 py-1 rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-xs)] text-[var(--text-secondary)]">
              <Clipboard class="w-3 h-3 text-[var(--text-muted)]" />
              <button
                type="button"
                onClick={() => {
                  setPasteModalData({
                    content: paste.content,
                    index: i(),
                    onSave: (idx, content) => {
                      if (props.onUpdatePaste) {
                        props.onUpdatePaste(idx, content)
                      }
                    },
                  })
                }}
                class="flex items-center gap-1 hover:text-[var(--accent)] transition-colors"
              >
                <span>
                  Pasted text · {paste.lineCount} line{paste.lineCount !== 1 ? 's' : ''}
                </span>
                <span class="text-[9px] opacity-40 ml-0.5">click to edit</span>
              </button>
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
  </>
)
