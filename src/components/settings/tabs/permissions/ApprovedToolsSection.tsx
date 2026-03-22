/**
 * Approved Tools Section
 *
 * Always-approved tool list with add/remove support.
 */

import { Plus, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

export interface ApprovedToolsSectionProps {
  tools: string[]
  onUpdateTools: (tools: string[]) => void
}

export const ApprovedToolsSection: Component<ApprovedToolsSectionProps> = (props) => {
  const [newApprovedTool, setNewApprovedTool] = createSignal('')

  const addApprovedTool = () => {
    const tool = newApprovedTool().trim()
    if (!tool || props.tools.includes(tool)) return
    props.onUpdateTools([...props.tools, tool])
    setNewApprovedTool('')
  }

  const removeApprovedTool = (tool: string) => {
    props.onUpdateTools(props.tools.filter((t) => t !== tool))
  }

  return (
    <div>
      <Show
        when={props.tools.length > 0}
        fallback={
          <p class="text-[var(--settings-text-description)] text-[var(--text-muted)] py-2">
            No always-approved tools configured.
          </p>
        }
      >
        <div class="flex flex-wrap gap-1 mb-2">
          <For each={props.tools}>
            {(tool) => (
              <span class="inline-flex items-center gap-1.5 px-2.5 py-1 text-[var(--settings-text-description)] bg-[var(--surface-raised)] text-[var(--text-secondary)] rounded-[var(--radius-md)] border border-[var(--border-subtle)]">
                <span class="font-mono">{tool}</span>
                <button
                  type="button"
                  onClick={() => removeApprovedTool(tool)}
                  class="text-[var(--text-muted)] hover:text-[var(--error)]"
                >
                  <X class="w-2.5 h-2.5" />
                </button>
              </span>
            )}
          </For>
        </div>
      </Show>
      <div class="flex items-center gap-2">
        <input
          type="text"
          value={newApprovedTool()}
          onInput={(e) => setNewApprovedTool(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') addApprovedTool()
          }}
          placeholder="Tool name (e.g. read_file, glob)"
          class="flex-1 px-3 py-2 text-[var(--settings-text-label)] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
        />
        <button
          type="button"
          onClick={addApprovedTool}
          disabled={!newApprovedTool().trim()}
          class="p-1 text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-md)] disabled:opacity-50"
        >
          <Plus class="w-4 h-4" />
        </button>
      </div>
    </div>
  )
}
