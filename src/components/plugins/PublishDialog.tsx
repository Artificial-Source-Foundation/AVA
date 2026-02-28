/**
 * Publish Plugin Dialog
 *
 * Stub dialog for publishing plugins to the marketplace.
 * No backend — just UI flow placeholder.
 */

import { Package, Upload, X } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'

interface PublishDialogProps {
  open: boolean
  onClose: () => void
}

export const PublishDialog: Component<PublishDialogProps> = (props) => {
  const [name, setName] = createSignal('')
  const [description, setDescription] = createSignal('')
  const [version, setVersion] = createSignal('0.1.0')
  const [step, setStep] = createSignal<'form' | 'preview' | 'done'>('form')

  const handlePublish = () => {
    setStep('done')
  }

  const handleClose = () => {
    setStep('form')
    setName('')
    setDescription('')
    setVersion('0.1.0')
    props.onClose()
  }

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
          <div class="flex items-center justify-between">
            <div class="flex items-center gap-2">
              <Upload class="w-4 h-4 text-[var(--accent)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">Publish Plugin</h3>
            </div>
            <button
              type="button"
              onClick={handleClose}
              class="p-1 text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          <Show when={step() === 'form'}>
            <div class="space-y-3">
              <label class="block">
                <span class="text-[11px] text-[var(--text-secondary)] mb-1 block">Plugin Name</span>
                <input
                  type="text"
                  value={name()}
                  onInput={(e) => setName(e.currentTarget.value)}
                  placeholder="my-awesome-plugin"
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                />
              </label>
              <label class="block">
                <span class="text-[11px] text-[var(--text-secondary)] mb-1 block">Description</span>
                <textarea
                  value={description()}
                  onInput={(e) => setDescription(e.currentTarget.value)}
                  placeholder="What does your plugin do?"
                  rows={3}
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none resize-none"
                />
              </label>
              <label class="block">
                <span class="text-[11px] text-[var(--text-secondary)] mb-1 block">Version</span>
                <input
                  type="text"
                  value={version()}
                  onInput={(e) => setVersion(e.currentTarget.value)}
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                />
              </label>
              <div class="flex gap-2 justify-end pt-2">
                <button
                  type="button"
                  onClick={handleClose}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                >
                  Cancel
                </button>
                <button
                  type="button"
                  onClick={() => setStep('preview')}
                  disabled={!name().trim()}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50"
                >
                  Preview
                </button>
              </div>
            </div>
          </Show>

          <Show when={step() === 'preview'}>
            <div class="space-y-3">
              <div class="p-3 rounded-[var(--radius-md)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)]">
                <div class="flex items-center gap-2 mb-2">
                  <Package class="w-4 h-4 text-[var(--accent)]" />
                  <span class="text-sm font-medium text-[var(--text-primary)]">{name()}</span>
                  <span class="text-[10px] text-[var(--text-muted)]">v{version()}</span>
                </div>
                <p class="text-xs text-[var(--text-secondary)]">
                  {description() || 'No description'}
                </p>
              </div>
              <p class="text-[10px] text-[var(--warning)]">
                Publishing is not yet connected to a backend registry. This is a preview of the
                publish flow.
              </p>
              <div class="flex gap-2 justify-end">
                <button
                  type="button"
                  onClick={() => setStep('form')}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                >
                  Back
                </button>
                <button
                  type="button"
                  onClick={handlePublish}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
                >
                  Publish
                </button>
              </div>
            </div>
          </Show>

          <Show when={step() === 'done'}>
            <div class="text-center py-4 space-y-3">
              <Package class="w-8 h-8 mx-auto text-[var(--success)]" />
              <p class="text-sm font-medium text-[var(--text-primary)]">Ready to Publish</p>
              <p class="text-xs text-[var(--text-muted)]">
                When the plugin registry is available, "{name()}" will be published automatically.
              </p>
              <button
                type="button"
                onClick={handleClose}
                class="px-4 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
              >
                Done
              </button>
            </div>
          </Show>
        </div>
      </div>
    </Show>
  )
}
