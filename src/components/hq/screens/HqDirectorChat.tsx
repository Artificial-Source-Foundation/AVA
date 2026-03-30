/**
 * HQ Director Chat
 *
 * Renders the normal ChatView in "director mode" by providing ChatModeOverrides.
 * All UI rendering goes through the exact same ChatView → MessageList → MessageInput
 * path as normal chat. Only data sources and actions differ.
 */

import { Brain, Crown, Search, ShieldCheck, Sparkles } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import type { ChatModeOverrides } from '../../../contexts/chat-mode'
import { useHq } from '../../../stores/hq'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import type { Message, ToolCall } from '../../../types'
import { ChatHeaderBar } from '../../chat/ChatHeaderBar'
import { ChatView } from '../../chat/ChatView'
import { ComposerToolbarDivider } from '../../chat/message-input/composer-toolbar-row'
import {
  aggregateModels,
  buildModelSpec,
  formatModelSelectionLabel,
} from '../../dialogs/model-browser/model-browser-helpers'

const HQ_SESSION_ID = 'hq-director'

// ── HQ Top Cards (injected into message list top area) ───────────────────

const HqTopCards: Component = () => {
  const { agents, activity, plan, lastBootstrapResult, navigateToAgent } = useHq()

  const runningWorkers = createMemo(() =>
    agents()
      .filter((agent) => agent.status === 'running')
      .slice(0, 3)
  )
  const completedWorkers = createMemo(() =>
    agents()
      .filter((agent) => agent.status === 'idle' && (agent.turn ?? 0) > 0)
      .slice()
      .sort((a, b) => (b.turn ?? 0) - (a.turn ?? 0))
      .slice(0, 2)
  )
  const latestActivity = createMemo(() => activity().slice(0, 4))

  const topCards = createMemo(() => {
    const cards: Array<{
      id: string
      tone: string
      border: string
      surface: string
      title: string
      body: string
      icon: 'scout' | 'memory' | 'qa' | 'phase'
    }> = []

    if (lastBootstrapResult()) {
      cards.push({
        id: 'memory-bootstrap',
        tone: '#5E5CE6',
        border: 'rgba(94,92,230,0.18)',
        surface: 'rgba(94,92,230,0.08)',
        title: lastBootstrapResult()!.reusedExisting
          ? 'Director notebook reused'
          : 'Director notebook initialized',
        body: lastBootstrapResult()!.reusedExisting
          ? `Using the existing .ava/HQ notebook for ${lastBootstrapResult()!.projectName}.`
          : `Created ${lastBootstrapResult()!.createdFiles.length} HQ memory files in .ava/HQ for ${lastBootstrapResult()!.projectName}.`,
        icon: 'memory',
      })
    }

    if (plan()) {
      cards.push({
        id: 'plan-note',
        tone: 'var(--warning)',
        border: 'rgba(245,166,35,0.18)',
        surface: 'rgba(245,166,35,0.08)',
        title: `${plan()!.title}`,
        body: `${plan()!.phases.length} phases are currently ${plan()!.status.replaceAll('-', ' ')}. Open Plan Review for approvals and edits.`,
        icon: 'phase',
      })
    }

    const scoutLike = latestActivity().find(
      (entry) => entry.type === 'planning' || entry.type === 'delegation'
    )
    if (scoutLike) {
      cards.push({
        id: 'scout-note',
        tone: '#0A84FF',
        border: 'rgba(10,132,255,0.18)',
        surface: 'rgba(10,132,255,0.08)',
        title: 'Scouting and planning update',
        body: scoutLike.message,
        icon: 'scout',
      })
    }

    const qaLike = latestActivity().find(
      (entry) => entry.type === 'review' || entry.type === 'completion'
    )
    if (qaLike) {
      cards.push({
        id: 'qa-note',
        tone: 'var(--success)',
        border: 'rgba(52,199,89,0.18)',
        surface: 'rgba(52,199,89,0.08)',
        title: 'QA and completion signal',
        body: qaLike.message,
        icon: 'qa',
      })
    }

    return cards.slice(0, 4)
  })

  return (
    <div class="mb-4 flex flex-col gap-3">
      <Show when={topCards().length > 0}>
        <div class="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <For each={topCards()}>
            {(card) => (
              <div
                class="rounded-xl border px-4 py-3"
                style={{
                  'border-color': card.border,
                  'background-color': card.surface,
                }}
              >
                <div class="flex items-start gap-3">
                  <div class="mt-0.5">
                    <Show when={card.icon === 'memory'}>
                      <Brain size={14} style={{ color: card.tone }} />
                    </Show>
                    <Show when={card.icon === 'scout'}>
                      <Search size={14} style={{ color: card.tone }} />
                    </Show>
                    <Show when={card.icon === 'qa'}>
                      <ShieldCheck size={14} style={{ color: card.tone }} />
                    </Show>
                    <Show when={card.icon === 'phase'}>
                      <Sparkles size={14} style={{ color: card.tone }} />
                    </Show>
                  </div>
                  <div class="min-w-0 flex-1">
                    <div
                      class="text-[11px] font-semibold uppercase tracking-[0.16em]"
                      style={{ color: card.tone }}
                    >
                      {card.title}
                    </div>
                    <div
                      class="mt-1 text-[12px] leading-relaxed"
                      style={{ color: 'var(--text-secondary)' }}
                    >
                      {card.body}
                    </div>
                  </div>
                </div>
              </div>
            )}
          </For>
        </div>
      </Show>

      <Show when={runningWorkers().length > 0}>
        <div class="grid grid-cols-1 gap-3 xl:grid-cols-3">
          <For each={runningWorkers()}>
            {(agent) => (
              <button
                type="button"
                class="rounded-xl border bg-[var(--surface)] px-4 py-3 text-left"
                style={{ 'border-color': 'var(--border-subtle)' }}
                onClick={() => navigateToAgent(agent.id)}
              >
                <div class="flex items-center justify-between gap-2">
                  <span class="text-[12px] font-semibold" style={{ color: 'var(--text-primary)' }}>
                    {agent.name}
                  </span>
                  <span class="text-[10px] font-medium" style={{ color: 'var(--success)' }}>
                    {agent.turn ?? 0}/{agent.maxTurns ?? 0}
                  </span>
                </div>
                <div class="mt-1 text-[11px]" style={{ color: 'var(--text-muted)' }}>
                  {agent.currentTask || 'Currently executing'}
                </div>
                <div class="mt-2 text-[10px]" style={{ color: 'var(--success)' }}>
                  active now
                </div>
              </button>
            )}
          </For>
        </div>
      </Show>

      <Show when={completedWorkers().length > 0}>
        <div class="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <For each={completedWorkers()}>
            {(agent) => (
              <div
                class="rounded-xl border px-4 py-3"
                style={{
                  'border-color': 'rgba(52,199,89,0.2)',
                  'background-color': 'rgba(52,199,89,0.08)',
                }}
              >
                <div
                  class="text-[11px] font-semibold uppercase tracking-[0.16em]"
                  style={{ color: 'var(--success)' }}
                >
                  Worker completion
                </div>
                <div
                  class="mt-1 text-[12px] font-semibold"
                  style={{ color: 'var(--text-primary)' }}
                >
                  {agent.name} wrapped a task cleanly
                </div>
                <div class="mt-1 text-[11px]" style={{ color: 'var(--text-muted)' }}>
                  {agent.filesTouched.length} files touched · ${agent.totalCostUsd.toFixed(2)}{' '}
                  reported
                </div>
              </div>
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}

// ── Director Chat Component ──────────────────────────────────────────────

const HqDirectorChat: Component = () => {
  const hq = useHq()
  const { selectedModel, selectedProvider } = useSession()
  const { settings } = useSettings()

  // ── Director model display ─────────────────────────────────────────────
  const enabledProviders = createMemo(() =>
    settings().providers.filter((provider) => provider.enabled || provider.status === 'connected')
  )
  const availableModels = createMemo(() => aggregateModels(enabledProviders()))
  const lastUsedModelSpec = createMemo(() =>
    selectedModel() ? buildModelSpec(selectedModel(), selectedProvider()) : ''
  )
  const directorModel = createMemo(() =>
    formatModelSelectionLabel(
      availableModels(),
      hq.hqSettings().directorModel || settings().team.defaultDirectorModel || lastUsedModelSpec(),
      { autoLabel: 'Auto (strongest available)' }
    )
  )

  // ── Map HQ messages to standard Message[] ──────────────────────────────
  const mapDelegationsToToolCalls = (
    messageId: string,
    timestamp: number,
    role: string,
    delegations: ReturnType<typeof hq.directorMessages>[number]['delegations']
  ): ToolCall[] => {
    if (role === 'user') return []
    return delegations.map((card, index) => ({
      id: `${messageId}-delegation-${index}`,
      name: 'delegate',
      args: { agent: card.agentName, task: card.task },
      status:
        card.status === 'done' ? 'success' : card.status === 'running' ? 'running' : 'pending',
      output: `${card.agentName}: ${card.task}`,
      startedAt: timestamp,
      completedAt: card.status === 'done' ? timestamp : undefined,
    }))
  }

  const mappedMessages = createMemo<Message[]>(() =>
    hq.directorMessages().map((msg) => ({
      id: msg.id,
      sessionId: HQ_SESSION_ID,
      role: msg.role === 'user' ? 'user' : 'assistant',
      content: msg.content,
      createdAt: msg.timestamp,
      model: msg.role === 'user' ? undefined : directorModel(),
      toolCalls: mapDelegationsToToolCalls(msg.id, msg.timestamp, msg.role, msg.delegations),
      metadata: { mode: 'HQ Director' },
    }))
  )

  const liveMessage = createMemo<Message | null>(() => {
    if (!hq.liveDirectorStreaming()) return null
    return {
      id: 'hq-live-director',
      sessionId: HQ_SESSION_ID,
      role: 'assistant',
      content: hq.liveDirectorContent(),
      createdAt: hq.liveDirectorStartedAt() ?? Date.now(),
      model: directorModel(),
      toolCalls: hq.liveDirectorToolCalls(),
      metadata: {
        mode: 'HQ Director',
        thinking: hq.liveDirectorThinking(),
        thinkingSegments: hq.liveDirectorThinkingSegments(),
      },
    }
  })

  const displayedMessages = createMemo(() => {
    const base = mappedMessages()
    const live = liveMessage()
    return live ? [...base, live] : base
  })

  // ── Build ChatModeOverrides ────────────────────────────────────────────
  const config: ChatModeOverrides = {
    mode: 'director',

    // Data sources
    messages: displayedMessages,
    isLoading: () => false,
    isStreaming: hq.liveDirectorStreaming,
    liveMessageId: () => (hq.liveDirectorStreaming() ? 'hq-live-director' : null),
    streamingContent: hq.liveDirectorContent,
    streamingToolCalls: hq.liveDirectorToolCalls,
    streamingThinkingSegments: hq.liveDirectorThinkingSegments,
    streamStartedAt: hq.liveDirectorStartedAt,

    // Actions
    sendMessage: (content: string) => void hq.sendDirectorMessage(content),

    // Behavior
    readOnly: true,
    hideDocks: true,

    // UI
    header: (
      <ChatHeaderBar
        title={
          <div class="flex items-center gap-2">
            <Crown size={16} style={{ color: '#f59e0b' }} />
            <span class="truncate text-sm font-medium text-[var(--text-primary)]">Director</span>
          </div>
        }
        leftMeta={
          <>
            <span
              class="shrink-0 rounded px-1.5 py-[2px] text-[10px] leading-tight"
              style={{
                color: 'var(--text-muted)',
                'background-color': 'var(--alpha-white-8)',
                'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
              }}
            >
              HQ
            </span>
            <span
              class="shrink-0 rounded px-1.5 py-[2px] text-[10px] leading-tight"
              style={{
                color: 'var(--warning)',
                'background-color': 'rgba(245,166,35,0.14)',
                'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
              }}
            >
              {directorModel()}
            </span>
          </>
        }
        right={
          <button
            type="button"
            class="text-[11px] text-[var(--accent)]"
            onClick={() => hq.navigateTo('plan-review', 'Plan Review')}
          >
            Open plan review
          </button>
        }
      />
    ),

    topContent: () => <HqTopCards />,

    placeholder: () =>
      hq.liveDirectorStreaming()
        ? 'Type a message... (Enter = send to Director, Shift+Enter = newline)'
        : 'Message HQ Director... (Ctrl+/ for commands)',

    modelDisplay: directorModel,

    toolbarExtra: () => (
      <>
        <ComposerToolbarDivider />
        <span class="inline-flex items-center gap-1 rounded-full px-2.5 py-1 bg-[var(--alpha-white-5)] text-[var(--text-secondary)]">
          <Crown size={12} style={{ color: '#f59e0b' }} />
          <span class="text-[10px]">Director</span>
        </span>
      </>
    ),
  }

  return <ChatView config={config} />
}

export default HqDirectorChat
