import { ChevronDown, ChevronRight } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

export interface PraxisEngineerNode {
  id: string
  task: string
  status: 'coding' | 'reviewing' | 'approved' | 'merging' | 'complete' | 'failed'
  reviewAttempts: number
}

export interface PraxisLeadNode {
  id: string
  domain: string
  status: 'pending' | 'active' | 'complete' | 'failed'
  engineers: PraxisEngineerNode[]
}

function statusDot(status: string): string {
  if (status === 'complete' || status === 'approved') return 'bg-[var(--success)]'
  if (
    status === 'active' ||
    status === 'coding' ||
    status === 'reviewing' ||
    status === 'merging'
  ) {
    return 'bg-[var(--warning)]'
  }
  if (status === 'failed') return 'bg-[var(--error)]'
  return 'bg-[var(--text-muted)]'
}

export const TeamCard: Component<{ lead: PraxisLeadNode }> = (props) => {
  const [expanded, setExpanded] = createSignal(true)

  return (
    <div class="rounded-[var(--radius-lg)] border border-[var(--border-default)] bg-[var(--surface)]">
      <button
        type="button"
        onClick={() => setExpanded(!expanded())}
        class="w-full flex items-center gap-2 px-3 py-2 text-left hover:bg-[var(--alpha-white-3)]"
      >
        <span class={`w-2 h-2 rounded-full ${statusDot(props.lead.status)}`} />
        <span class="font-[var(--font-ui-mono)] text-[12px] font-semibold text-[var(--text-primary)]">
          Tech Lead: {props.lead.domain}
        </span>
        <span class="ml-auto text-[10px] text-[var(--text-muted)]">
          {props.lead.engineers.length}
        </span>
        <Show
          when={expanded()}
          fallback={<ChevronRight class="w-3 h-3 text-[var(--text-muted)]" />}
        >
          <ChevronDown class="w-3 h-3 text-[var(--text-muted)]" />
        </Show>
      </button>

      <Show when={expanded()}>
        <div class="px-3 pb-2 space-y-1">
          <For each={props.lead.engineers}>
            {(engineer) => (
              <div class="flex items-center gap-2 text-[11px] text-[var(--text-secondary)]">
                <span class={`w-1.5 h-1.5 rounded-full ${statusDot(engineer.status)}`} />
                <span class="truncate">
                  Engineer {engineer.id.slice(0, 8)}: {engineer.task}
                </span>
                <span class="ml-auto text-[10px] text-[var(--text-muted)]">
                  {engineer.status} ({engineer.reviewAttempts}x)
                </span>
              </div>
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}
