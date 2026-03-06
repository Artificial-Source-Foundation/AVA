import { Crown } from 'lucide-solid'
import { type Component, createSignal, For, onCleanup, onMount, Show } from 'solid-js'
import { type PraxisLeadNode, TeamCard } from './team/TeamCard'

type PraxisMode = 'full' | 'light' | 'solo'

interface PraxisProgress {
  mode: PraxisMode
  leads: PraxisLeadNode[]
}

const INITIAL_PROGRESS: PraxisProgress = { mode: 'light', leads: [] }

function countComplete(progress: PraxisProgress): [number, number] {
  let total = 0
  let complete = 0
  for (const lead of progress.leads) {
    for (const engineer of lead.engineers) {
      total += 1
      if (engineer.status === 'complete') complete += 1
    }
  }
  return [complete, total]
}

export const TeamPanel: Component = () => {
  const [progress, setProgress] = createSignal<PraxisProgress>(INITIAL_PROGRESS)

  onMount(() => {
    const handler = (event: Event) => {
      const custom = event as CustomEvent<PraxisProgress>
      if (custom.detail) setProgress(custom.detail)
    }

    window.addEventListener('praxis:progress-updated', handler as EventListener)
    onCleanup(() => window.removeEventListener('praxis:progress-updated', handler as EventListener))
  })

  const summary = () => countComplete(progress())

  return (
    <div class="flex flex-col h-full bg-[var(--bg-primary)]">
      <div class="h-10 px-3 flex items-center justify-between border-b border-[var(--border-subtle)]">
        <span class="font-[var(--font-ui-mono)] text-[11px] tracking-widest uppercase text-[var(--text-secondary)]">
          Praxis Team
        </span>
        <span class="text-[10px] text-[var(--text-muted)]">
          {progress().mode.toUpperCase()} mode
        </span>
      </div>

      <div class="p-3 space-y-2 overflow-y-auto scrollbar-none">
        <div class="rounded-[var(--radius-lg)] border border-[var(--border-default)] bg-[var(--surface)] px-3 py-2 flex items-center gap-2">
          <div class="p-1 rounded-[var(--radius-md)] bg-[var(--accent-subtle)]">
            <Crown class="w-3.5 h-3.5 text-[var(--accent)]" />
          </div>
          <div>
            <div class="text-[12px] font-semibold text-[var(--text-primary)]">Director</div>
            <div class="text-[10px] text-[var(--text-muted)]">Orchestrating hierarchy</div>
          </div>
        </div>

        <Show
          when={progress().leads.length > 0}
          fallback={
            <div class="text-[11px] text-[var(--text-muted)] px-1">Waiting for assignments...</div>
          }
        >
          <For each={progress().leads}>{(lead) => <TeamCard lead={lead} />}</For>
        </Show>
      </div>

      <div class="mt-auto border-t border-[var(--border-subtle)] px-3 py-2 text-[11px] text-[var(--text-secondary)]">
        Overall: {summary()[0]}/{summary()[1]} engineers complete
      </div>
    </div>
  )
}
