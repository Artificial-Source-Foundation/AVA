/**
 * Export Options Dialog
 * Modal with redaction checkboxes and export button for conversation export.
 */

import { Download, ShieldCheck } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { DEFAULT_EXPORT_OPTIONS, type ExportOptions } from '../../lib/export-conversation'

interface ExportOptionsDialogProps {
  open: boolean
  onClose: () => void
  onExport: (options: ExportOptions) => void
}

export const ExportOptionsDialog: Component<ExportOptionsDialogProps> = (props) => {
  const [stripApiKeys, setStripApiKeys] = createSignal(
    DEFAULT_EXPORT_OPTIONS.redaction.stripApiKeys
  )
  const [stripFilePaths, setStripFilePaths] = createSignal(
    DEFAULT_EXPORT_OPTIONS.redaction.stripFilePaths
  )
  const [stripEmails, setStripEmails] = createSignal(DEFAULT_EXPORT_OPTIONS.redaction.stripEmails)
  const [includeMetadata, setIncludeMetadata] = createSignal(DEFAULT_EXPORT_OPTIONS.includeMetadata)
  const [includeArtifacts, setIncludeArtifacts] = createSignal(
    DEFAULT_EXPORT_OPTIONS.includeArtifacts
  )

  const handleExport = () => {
    props.onExport({
      redaction: {
        stripApiKeys: stripApiKeys(),
        stripFilePaths: stripFilePaths(),
        stripEmails: stripEmails(),
      },
      includeMetadata: includeMetadata(),
      includeArtifacts: includeArtifacts(),
    })
    props.onClose()
  }

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-sm w-full shadow-2xl space-y-4">
          <div class="flex items-center gap-2">
            <Download class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Export Conversation</h3>
          </div>

          {/* Redaction Options */}
          <div class="space-y-2">
            <div class="flex items-center gap-2 text-xs text-[var(--text-muted)]">
              <ShieldCheck class="w-3.5 h-3.5" />
              <span class="font-medium uppercase tracking-wide text-[10px]">Redaction</span>
            </div>
            <label class="flex items-center gap-2.5 py-1 cursor-pointer">
              <input
                type="checkbox"
                checked={stripApiKeys()}
                onChange={(e) => setStripApiKeys(e.currentTarget.checked)}
                class="w-3.5 h-3.5 rounded accent-[var(--accent)]"
              />
              <div>
                <span class="text-xs text-[var(--text-secondary)]">Strip API keys</span>
                <p class="text-[10px] text-[var(--text-muted)]">
                  Removes sk-..., key-..., Bearer tokens, etc.
                </p>
              </div>
            </label>
            <label class="flex items-center gap-2.5 py-1 cursor-pointer">
              <input
                type="checkbox"
                checked={stripFilePaths()}
                onChange={(e) => setStripFilePaths(e.currentTarget.checked)}
                class="w-3.5 h-3.5 rounded accent-[var(--accent)]"
              />
              <div>
                <span class="text-xs text-[var(--text-secondary)]">Strip file paths</span>
                <p class="text-[10px] text-[var(--text-muted)]">
                  Removes absolute paths (/home/..., C:\...)
                </p>
              </div>
            </label>
            <label class="flex items-center gap-2.5 py-1 cursor-pointer">
              <input
                type="checkbox"
                checked={stripEmails()}
                onChange={(e) => setStripEmails(e.currentTarget.checked)}
                class="w-3.5 h-3.5 rounded accent-[var(--accent)]"
              />
              <div>
                <span class="text-xs text-[var(--text-secondary)]">Strip emails</span>
                <p class="text-[10px] text-[var(--text-muted)]">Removes email addresses</p>
              </div>
            </label>
          </div>

          {/* Content Options */}
          <div class="space-y-2 pt-1 border-t border-[var(--border-subtle)]">
            <span class="font-medium uppercase tracking-wide text-[10px] text-[var(--text-muted)]">
              Content
            </span>
            <label class="flex items-center gap-2.5 py-1 cursor-pointer">
              <input
                type="checkbox"
                checked={includeMetadata()}
                onChange={(e) => setIncludeMetadata(e.currentTarget.checked)}
                class="w-3.5 h-3.5 rounded accent-[var(--accent)]"
              />
              <span class="text-xs text-[var(--text-secondary)]">Include session metadata</span>
            </label>
            <label class="flex items-center gap-2.5 py-1 cursor-pointer">
              <input
                type="checkbox"
                checked={includeArtifacts()}
                onChange={(e) => setIncludeArtifacts(e.currentTarget.checked)}
                class="w-3.5 h-3.5 rounded accent-[var(--accent)]"
              />
              <span class="text-xs text-[var(--text-secondary)]">Include artifacts summary</span>
            </label>
          </div>

          {/* Actions */}
          <div class="flex gap-2 justify-end pt-1">
            <button
              type="button"
              onClick={props.onClose}
              class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={handleExport}
              class="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
            >
              <Download class="w-3 h-3" />
              Export
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
