import { Building2, Clock3, Cpu, FileText, ShieldCheck, Sparkles } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { HqAgent } from '../../../types/hq'

function zoneTone(status: string): string {
  if (status === 'running' || status === 'active') return 'var(--success)'
  if (status === 'error') return 'var(--error)'
  if (status === 'paused') return 'var(--warning)'
  return 'var(--text-muted)'
}

function isQaAgent(agent: HqAgent): boolean {
  const role = agent.role.toLowerCase()
  return role.includes('qa') || role.includes('review') || agent.tier === 'scout'
}

const HqOrgChart: Component = () => {
  const { agents, selectedAgent, navigateToAgent, metrics } = useHq()

  const director = createMemo(() => agents().find((agent) => agent.tier === 'director') || null)
  const qa = createMemo(() =>
    agents().filter((agent) => agent.tier !== 'director' && isQaAgent(agent))
  )
  const engineering = createMemo(() =>
    agents().filter((agent) => agent.tier !== 'director' && !isQaAgent(agent))
  )
  const fallbackAgent = createMemo(
    () =>
      selectedAgent() ||
      agents().find((agent) => agent.status === 'running') ||
      director() ||
      agents()[0] ||
      null
  )

  return (
    <div class="flex h-full bg-[var(--background)]">
      <div class="flex min-w-0 flex-1 flex-col px-6 pb-6 pt-6">
        <div class="mb-4 flex items-center justify-between">
          <div>
            <h2 class="text-base font-semibold" style={{ color: 'var(--text-primary)' }}>
              Team Office
            </h2>
            <p class="mt-1 text-[12px]" style={{ color: 'var(--text-muted)' }}>
              A live floor view of who is coding, reviewing, waiting, or escalating work.
            </p>
          </div>
          <div class="flex items-center gap-2 text-[11px]" style={{ color: 'var(--text-muted)' }}>
            <Sparkles size={14} />
            {metrics().agentsRunning} agents currently moving
          </div>
        </div>

        <div
          class="relative min-h-0 flex-1 overflow-hidden rounded-2xl border"
          style={{
            'background-color': '#0b0d10',
            'border-color': 'var(--border-subtle)',
            background:
              'radial-gradient(circle at top, rgba(245,166,35,0.08), transparent 22%), linear-gradient(180deg, rgba(255,255,255,0.02), rgba(255,255,255,0))',
          }}
        >
          <div
            class="absolute inset-x-6 top-6 rounded-2xl border px-5 py-4"
            style={{
              'border-color': 'rgba(245,166,35,0.14)',
              'background-color': 'rgba(245,166,35,0.05)',
            }}
          >
            <div class="flex items-center justify-between gap-4">
              <div class="flex items-center gap-3">
                <div class="flex h-10 w-10 items-center justify-center rounded-xl bg-[rgba(245,166,35,0.14)]">
                  <Building2 size={18} style={{ color: 'var(--warning)' }} />
                </div>
                <div>
                  <div
                    class="text-[11px] font-semibold uppercase tracking-[0.18em]"
                    style={{ color: 'var(--text-muted)' }}
                  >
                    Director Office
                  </div>
                  <div class="mt-1 text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                    {director()?.name || 'Director'}
                  </div>
                  <div class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                    {director()?.currentTask || 'Supervising the current mission'}
                  </div>
                </div>
              </div>
              <div class="text-right text-[11px]" style={{ color: 'var(--warning)' }}>
                <div>Amber office = command center</div>
                <div class="mt-1 text-[10px]" style={{ color: 'var(--text-muted)' }}>
                  Plans, memory, and escalations converge here
                </div>
              </div>
            </div>
          </div>

          <div class="absolute inset-x-6 bottom-6 top-28 grid grid-cols-[1fr_1fr] gap-6">
            <Zone
              title="Engineering Wing"
              subtitle="Implementation workers and leads"
              icon={Cpu}
              agents={engineering()}
              onSelect={navigateToAgent}
            />
            <Zone
              title="QA Wing"
              subtitle="Reviewers and verification workers"
              icon={ShieldCheck}
              agents={qa()}
              onSelect={navigateToAgent}
            />
          </div>
        </div>
      </div>

      <aside
        class="flex w-[360px] shrink-0 flex-col border-l px-5 py-6"
        style={{ 'border-color': 'var(--border-subtle)', 'background-color': '#101114' }}
      >
        <div>
          <div
            class="text-[10px] font-semibold uppercase tracking-[0.18em]"
            style={{ color: 'var(--text-muted)' }}
          >
            Inspector
          </div>
          <div class="mt-2 text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
            {fallbackAgent()?.name || 'No agent selected'}
          </div>
          <div
            class="mt-1 text-[12px]"
            style={{ color: zoneTone(fallbackAgent()?.status || 'idle') }}
          >
            {fallbackAgent()?.role || 'Select a person from the office to inspect their work'}
          </div>
        </div>

        <Show when={fallbackAgent()}>
          {(agent) => (
            <>
              <div
                class="mt-4 rounded-xl border p-4"
                style={{
                  'border-color': 'var(--border-subtle)',
                  'background-color': 'rgba(255,255,255,0.02)',
                }}
              >
                <div
                  class="flex items-center justify-between text-[11px]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  <span>Status</span>
                  <span style={{ color: zoneTone(agent().status) }}>{agent().status}</span>
                </div>
                <div class="mt-3 text-[12px] font-medium" style={{ color: 'var(--text-primary)' }}>
                  {agent().currentTask || 'No active task right now'}
                </div>
                <div
                  class="mt-3 flex items-center justify-between text-[11px]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  <span>Turns</span>
                  <span>
                    {agent().turn ?? 0}/{agent().maxTurns ?? 0}
                  </span>
                </div>
                <div
                  class="mt-2 flex items-center justify-between text-[11px]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  <span>Cost</span>
                  <span>${agent().totalCostUsd.toFixed(2)}</span>
                </div>
              </div>

              <div class="mt-4">
                <div
                  class="flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.18em]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  <Clock3 size={12} />
                  Recent Transcript
                </div>
                <div class="mt-3 flex max-h-[260px] flex-col gap-2 overflow-y-auto">
                  <For each={agent().transcript.slice(-8).reverse()}>
                    {(entry) => (
                      <div
                        class="rounded-lg border px-3 py-2"
                        style={{
                          'border-color': 'var(--border-subtle)',
                          'background-color': 'rgba(255,255,255,0.015)',
                        }}
                      >
                        <div
                          class="flex items-center justify-between gap-3 text-[10px]"
                          style={{ color: 'var(--text-muted)' }}
                        >
                          <span>{entry.type}</span>
                          <Show when={entry.toolName}>
                            <span class="font-mono">{entry.toolName}</span>
                          </Show>
                        </div>
                        <div
                          class="mt-1 text-[11px] leading-relaxed"
                          style={{ color: 'var(--text-secondary)' }}
                        >
                          {entry.content}
                        </div>
                      </div>
                    )}
                  </For>
                </div>
              </div>

              <div class="mt-4">
                <div
                  class="flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.18em]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  <FileText size={12} />
                  Files Touched
                </div>
                <div class="mt-3 flex flex-wrap gap-2">
                  <For each={agent().filesTouched.slice(0, 8)}>
                    {(path) => (
                      <span
                        class="rounded-full px-2.5 py-1 text-[10px] font-mono"
                        style={{
                          color: 'var(--text-secondary)',
                          'background-color': 'rgba(255,255,255,0.04)',
                        }}
                      >
                        {path}
                      </span>
                    )}
                  </For>
                </div>
              </div>
            </>
          )}
        </Show>
      </aside>
    </div>
  )
}

const Zone: Component<{
  title: string
  subtitle: string
  icon: Component<{ size?: number; class?: string }>
  agents: HqAgent[]
  onSelect: (id: string) => void
}> = (props) => (
  <section
    class="rounded-2xl border p-5"
    style={{ 'border-color': 'var(--border-subtle)', 'background-color': 'rgba(255,255,255,0.02)' }}
  >
    <div class="flex items-center gap-3">
      <div class="flex h-10 w-10 items-center justify-center rounded-xl bg-[rgba(255,255,255,0.04)]">
        <props.icon size={18} class="text-[var(--text-secondary)]" />
      </div>
      <div>
        <div class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
          {props.title}
        </div>
        <div class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
          {props.subtitle}
        </div>
      </div>
    </div>

    <div class="mt-5 grid grid-cols-2 gap-3">
      <For each={props.agents}>
        {(agent) => (
          <button
            type="button"
            class="rounded-xl border px-3 py-3 text-left transition-colors hover:bg-[rgba(255,255,255,0.03)]"
            style={{ 'border-color': 'var(--border-subtle)', 'background-color': '#0f1115' }}
            onClick={() => props.onSelect(agent.id)}
          >
            <div class="flex items-center justify-between gap-3">
              <span class="text-[12px] font-semibold" style={{ color: 'var(--text-primary)' }}>
                {agent.name}
              </span>
              <span
                class="h-2.5 w-2.5 rounded-full"
                style={{ 'background-color': zoneTone(agent.status) }}
              />
            </div>
            <div class="mt-2 text-[11px]" style={{ color: 'var(--text-muted)' }}>
              {agent.role}
            </div>
            <div
              class="mt-3 rounded-lg px-2 py-1.5 text-[10px] font-mono"
              style={{
                color: zoneTone(agent.status),
                'background-color': 'rgba(255,255,255,0.04)',
              }}
            >
              {agent.currentTask || agent.status}
            </div>
          </button>
        )}
      </For>
    </div>
  </section>
)

export default HqOrgChart
