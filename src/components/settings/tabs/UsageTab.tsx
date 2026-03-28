/**
 * Usage Tab — Subscription usage tracking for OAuth and credit-based providers.
 *
 * Shows plan tier, usage bars, credits remaining, and reset times
 * for OpenAI, Anthropic, GitHub Copilot, and OpenRouter.
 */

import { AlertTriangle, RefreshCw } from 'lucide-solid'
import { type Component, createSignal, For, onMount, Show } from 'solid-js'
import { rustBackend } from '../../../services/rust-bridge'
import type { CopilotQuota, SubscriptionUsage, UsageWindow } from '../../../types/rust-ipc'
import { getProviderLogo } from '../../icons/provider-logo-map'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'

function usageBarColor(percent: number): string {
  if (percent >= 85) return '#e5484d'
  if (percent >= 60) return '#f5a623'
  return '#30a46c'
}

function formatResetTime(isoOrTimestamp: string | null): string | null {
  if (!isoOrTimestamp) return null
  try {
    const ts = Number(isoOrTimestamp)
    const date = Number.isNaN(ts) ? new Date(isoOrTimestamp) : new Date(ts * 1000)
    const now = Date.now()
    const diffMs = date.getTime() - now
    if (diffMs <= 0) return 'now'
    const hours = Math.floor(diffMs / 3_600_000)
    const minutes = Math.floor((diffMs % 3_600_000) / 60_000)
    if (hours > 24) {
      const days = Math.floor(hours / 24)
      return `${days}d ${hours % 24}h`
    }
    if (hours > 0) return `${hours}h ${minutes}m`
    return `${minutes}m`
  } catch {
    return null
  }
}

const UsageBar: Component<{ window: UsageWindow }> = (props) => {
  const percent = () => props.window.usedPercent
  const reset = () => formatResetTime(props.window.resetsAt)

  return (
    <div class="flex flex-col gap-1">
      <div class="flex items-center justify-between text-[11px]">
        <span style={{ color: 'var(--text-secondary)' }}>{props.window.label}</span>
        <span style={{ color: 'var(--text-muted)' }}>
          {percent().toFixed(0)}% used
          <Show when={reset()}>
            {' '}
            <span style={{ color: 'var(--text-muted)' }}>· resets in {reset()}</span>
          </Show>
        </span>
      </div>
      <div
        class="h-2.5 rounded-full overflow-hidden"
        style={{ 'background-color': 'var(--gray-5)' }}
      >
        <div
          class="h-full rounded-full transition-all"
          style={{
            width: `${Math.min(100, percent())}%`,
            'background-color': usageBarColor(percent()),
          }}
        />
      </div>
    </div>
  )
}

const CopilotQuotaDisplay: Component<{ quota: CopilotQuota }> = (props) => {
  const q = () => props.quota
  const reset = () => formatResetTime(q().resetTime)
  const isUnlimited = () => q().limit < 0
  const usedPercent = () => (isUnlimited() ? 0 : Math.min(100, 100 - q().percentRemaining))
  const compUnlimited = () => (q().completionsLimit ?? 0) < 0

  return (
    <div class="flex flex-col gap-2">
      {/* Premium interactions quota */}
      <div class="flex flex-col gap-1">
        <div class="flex items-center justify-between text-[11px]">
          <span style={{ color: 'var(--text-secondary)' }}>Premium Requests</span>
          <span style={{ color: 'var(--text-muted)' }}>
            <Show when={!isUnlimited()} fallback="Unlimited">
              {q().remaining} / {q().limit} remaining
            </Show>
          </span>
        </div>
        <Show when={!isUnlimited()}>
          <div
            class="h-2.5 rounded-full overflow-hidden"
            style={{ 'background-color': 'var(--gray-5)' }}
          >
            <div
              class="h-full rounded-full transition-all"
              style={{
                width: `${usedPercent()}%`,
                'background-color': usageBarColor(usedPercent()),
              }}
            />
          </div>
        </Show>
      </div>

      {/* Completions quota */}
      <Show when={q().completionsRemaining != null && q().completionsLimit != null}>
        <div class="flex items-center justify-between text-[11px]">
          <span style={{ color: 'var(--text-secondary)' }}>Completions</span>
          <span style={{ color: 'var(--text-muted)' }}>
            <Show when={!compUnlimited()} fallback="Unlimited">
              {q().completionsRemaining} / {q().completionsLimit} remaining
            </Show>
          </span>
        </div>
      </Show>

      <Show when={reset()}>
        <div class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
          Resets in {reset()}
        </div>
      </Show>
    </div>
  )
}

const PlanBadge: Component<{ plan: string }> = (props) => (
  <span
    class="inline-flex items-center rounded-md px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wide"
    style={{
      'background-color': 'var(--accent-subtle)',
      color: 'var(--accent)',
    }}
  >
    {props.plan}
  </span>
)

const ProviderUsageCard: Component<{ usage: SubscriptionUsage }> = (props) => {
  const u = () => props.usage
  const hasData = () =>
    u().usageWindows.length > 0 || u().credits !== null || u().copilotQuota !== null

  return (
    <SettingsCard
      icon={getProviderLogo(u().provider)}
      title={u().displayName}
      description={u().error && !hasData() ? u().error! : undefined}
    >
      <div class="flex flex-col gap-3">
        {/* Plan badge */}
        <Show when={u().planType}>
          <div>
            <PlanBadge plan={u().planType!} />
          </div>
        </Show>

        {/* Usage windows (Anthropic, OpenAI, OpenRouter) */}
        <Show when={u().usageWindows.length > 0}>
          <div class="flex flex-col gap-2">
            <For each={u().usageWindows}>{(w) => <UsageBar window={w} />}</For>
          </div>
        </Show>

        {/* Copilot-specific quota */}
        <Show when={u().copilotQuota}>
          <CopilotQuotaDisplay quota={u().copilotQuota!} />
        </Show>

        {/* Credits */}
        <Show when={u().credits}>
          <div class="flex items-center justify-between text-[12px]">
            <span style={{ color: 'var(--text-secondary)' }}>Balance</span>
            <span class="font-mono font-medium" style={{ color: 'var(--text-primary)' }}>
              {u().credits!.unlimited ? 'Unlimited' : (u().credits!.balance ?? 'N/A')}
            </span>
          </div>
        </Show>

        {/* Error with partial data */}
        <Show when={u().error && hasData()}>
          <div
            class="flex items-center gap-1.5 text-[11px] rounded-md px-2 py-1"
            style={{
              color: 'var(--amber-9)',
              'background-color': 'rgba(255,200,0,0.06)',
            }}
          >
            <AlertTriangle class="w-3 h-3 flex-shrink-0" />
            <span>{u().error}</span>
          </div>
        </Show>
      </div>
    </SettingsCard>
  )
}

export const UsageTab: Component = () => {
  const [usageData, setUsageData] = createSignal<SubscriptionUsage[] | null>(null)
  const [loading, setLoading] = createSignal(false)
  const [error, setError] = createSignal(false)

  const fetchUsage = async () => {
    setLoading(true)
    setError(false)
    try {
      const data = await rustBackend.getSubscriptionUsage()
      setUsageData(data)
    } catch {
      setError(true)
    } finally {
      setLoading(false)
    }
  }

  onMount(() => void fetchUsage())

  return (
    <div class="flex flex-col" style={{ gap: SETTINGS_CARD_GAP }}>
      {/* Header with refresh */}
      <div class="flex items-center justify-between">
        <div>
          <h2 class="text-[15px] font-semibold" style={{ color: 'var(--text-primary)' }}>
            Subscription Usage
          </h2>
          <p class="text-[12px] mt-0.5" style={{ color: 'var(--text-muted)' }}>
            Plan tiers and remaining quota for connected providers.
          </p>
        </div>
        <button
          type="button"
          class="inline-flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-[12px] font-medium transition-colors"
          style={{
            'border-color': 'var(--border-default)',
            color: 'var(--text-secondary)',
            'background-color': 'var(--surface)',
          }}
          onClick={() => void fetchUsage()}
          disabled={loading()}
        >
          <RefreshCw class={`w-3.5 h-3.5 ${loading() ? 'animate-spin' : ''}`} />
          Refresh
        </button>
      </div>

      {/* Loading state */}
      <Show when={loading() && !usageData()}>
        <div class="text-[12px] py-8 text-center" style={{ color: 'var(--text-muted)' }}>
          Fetching usage data...
        </div>
      </Show>

      {/* Provider cards */}
      <Show when={usageData()}>
        <For each={usageData()!}>{(usage) => <ProviderUsageCard usage={usage} />}</For>
      </Show>

      {/* Error state */}
      <Show when={error()}>
        <div
          class="text-[12px] py-4 text-center rounded-lg"
          style={{ color: '#e5484d', 'background-color': 'rgba(255,0,0,0.08)' }}
        >
          Failed to fetch usage data. Check your credentials in Providers settings.
        </div>
      </Show>
    </div>
  )
}
