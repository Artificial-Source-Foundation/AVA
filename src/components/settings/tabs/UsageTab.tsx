/**
 * Usage Tab — Subscription usage tracking for OAuth and credit-based providers.
 *
 * Pencil design: "Subscription Usage" title (22px/600 #F5F5F7),
 * subtitle (12px #48484A), Refresh button (rounded-8, #ffffff0a border).
 * Provider cards: #111114, rounded-12, #ffffff08 border, 20px padding.
 * Plan badge: PRO in #0A84FF on #0A84FF18, rounded-6, 10px/600 uppercase.
 * Usage bars: 10px height, rounded-5, track #2C2C2E.
 * Bar colors: green #34C759 (<60%), amber #F5A623 (60-85%), red #e5484d (>85%).
 */

import { AlertTriangle, RefreshCw } from 'lucide-solid'
import { type Component, createSignal, For, onMount, Show } from 'solid-js'
import { rustBackend } from '../../../services/rust-bridge'
import type { CopilotQuota, SubscriptionUsage, UsageWindow } from '../../../types/rust-ipc'

function usageBarColor(percent: number): string {
  if (percent >= 85) return '#e5484d'
  if (percent >= 60) return '#F5A623'
  return '#34C759'
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
  const percent = () => Math.max(0, props.window.usedPercent)
  const reset = () => formatResetTime(props.window.resetsAt)

  return (
    <div class="flex flex-col" style={{ gap: '4px' }}>
      <div class="flex items-center justify-between">
        <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '11px', color: '#C8C8CC' }}>
          {props.window.label}
        </span>
        <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '11px', color: '#48484A' }}>
          {percent().toFixed(0)}% used
          <Show when={reset()}> · resets in {reset()}</Show>
        </span>
      </div>
      <div
        style={{
          height: '10px',
          'border-radius': '5px',
          background: '#2C2C2E',
          overflow: 'hidden',
        }}
      >
        <div
          style={{
            height: '100%',
            width: `${Math.min(100, percent())}%`,
            'border-radius': '5px',
            background: usageBarColor(percent()),
            transition: 'width 300ms ease',
          }}
        />
      </div>
    </div>
  )
}

const CopilotQuotaDisplay: Component<{ quota: CopilotQuota }> = (props) => {
  const q = () => props.quota
  const isUnlimited = () => q().limit < 0
  const percent = () => (q().limit > 0 ? ((q().limit - q().remaining) / q().limit) * 100 : 0)

  return (
    <div class="flex flex-col" style={{ gap: '4px' }}>
      <div class="flex items-center justify-between">
        <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '11px', color: '#C8C8CC' }}>
          Premium Requests
        </span>
        <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '11px', color: '#48484A' }}>
          <Show when={!isUnlimited()} fallback="Unlimited">
            {q().remaining} / {q().limit} remaining
          </Show>
        </span>
      </div>
      <Show when={!isUnlimited()}>
        <div
          style={{
            height: '10px',
            'border-radius': '5px',
            background: '#2C2C2E',
            overflow: 'hidden',
          }}
        >
          <div
            style={{
              height: '100%',
              width: `${Math.min(100, percent())}%`,
              'border-radius': '5px',
              background: usageBarColor(percent()),
              transition: 'width 300ms ease',
            }}
          />
        </div>
      </Show>
    </div>
  )
}

const PlanBadge: Component<{ plan: string }> = (props) => (
  <span
    style={{
      display: 'inline-flex',
      'align-items': 'center',
      'border-radius': '6px',
      padding: '2px 8px',
      background: '#0A84FF18',
      'font-family': 'Geist, sans-serif',
      'font-size': '10px',
      'font-weight': '600',
      'letter-spacing': '1px',
      color: '#0A84FF',
      'text-transform': 'uppercase',
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
    <div
      style={{
        display: 'flex',
        'flex-direction': 'column',
        gap: '16px',
        background: '#111114',
        border: '1px solid #ffffff08',
        'border-radius': '12px',
        padding: '20px',
      }}
    >
      {/* Provider header with optional plan badge */}
      <div class="flex items-center" style={{ gap: '10px' }}>
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '14px',
            'font-weight': '500',
            color: '#F5F5F7',
          }}
        >
          {u().displayName}
        </span>
        <Show when={u().planType}>
          <PlanBadge plan={u().planType!} />
        </Show>
      </div>

      {/* Usage windows (Anthropic, OpenAI, OpenRouter) */}
      <Show when={u().usageWindows.length > 0}>
        <div class="flex flex-col" style={{ gap: '4px' }}>
          <For each={u().usageWindows}>{(w) => <UsageBar window={w} />}</For>
        </div>
      </Show>

      {/* Copilot-specific quota */}
      <Show when={u().copilotQuota}>
        <CopilotQuotaDisplay quota={u().copilotQuota!} />
      </Show>

      {/* Credits / Balance */}
      <Show when={u().credits}>
        <div class="flex items-center justify-between">
          <span
            style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#C8C8CC' }}
          >
            Balance
          </span>
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              'font-weight': '500',
              color: '#F5F5F7',
            }}
          >
            {u().credits!.unlimited ? 'Unlimited' : (u().credits!.balance ?? 'N/A')}
          </span>
        </div>
      </Show>

      {/* Error indicator */}
      <Show when={u().error && !hasData()}>
        <span style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#48484A' }}>
          {u().error}
        </span>
      </Show>

      {/* Error with partial data */}
      <Show when={u().error && hasData()}>
        <div
          class="flex items-center gap-1.5"
          style={{
            'border-radius': '8px',
            padding: '6px 10px',
            background: 'rgba(255,200,0,0.06)',
          }}
        >
          <AlertTriangle class="w-3 h-3 shrink-0" style={{ color: '#F5A623' }} />
          <span
            style={{ 'font-family': 'Geist, sans-serif', 'font-size': '11px', color: '#F5A623' }}
          >
            {u().error}
          </span>
        </div>
      </Show>
    </div>
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
    <div class="flex flex-col" style={{ gap: '24px' }}>
      {/* Header with refresh */}
      <div class="flex items-center justify-between">
        <div class="flex flex-col" style={{ gap: '4px' }}>
          <h2
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '22px',
              'font-weight': '600',
              color: '#F5F5F7',
              margin: '0',
            }}
          >
            Subscription Usage
          </h2>
          <p
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '12px',
              color: '#48484A',
              margin: '0',
            }}
          >
            Plan tiers and remaining quota for connected providers.
          </p>
        </div>
        <button
          type="button"
          onClick={() => void fetchUsage()}
          disabled={loading()}
          class="flex items-center shrink-0"
          style={{
            gap: '6px',
            'border-radius': '8px',
            padding: '6px 10px',
            border: '1px solid #ffffff0a',
            background: 'transparent',
            cursor: 'pointer',
            'font-family': 'Geist, sans-serif',
            'font-size': '12px',
            'font-weight': '500',
            color: '#C8C8CC',
          }}
        >
          <RefreshCw
            class={`w-3.5 h-3.5 ${loading() ? 'animate-spin' : ''}`}
            style={{ color: '#C8C8CC' }}
          />
          Refresh
        </button>
      </div>

      {/* Loading state */}
      <Show when={loading() && !usageData()}>
        <div
          class="py-8 text-center"
          style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#48484A' }}
        >
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
          class="text-center"
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '12px',
            padding: '16px',
            'border-radius': '12px',
            color: '#e5484d',
            background: 'rgba(255,0,0,0.08)',
          }}
        >
          Failed to fetch usage data. Check your credentials in Providers settings.
        </div>
      </Show>
    </div>
  )
}
