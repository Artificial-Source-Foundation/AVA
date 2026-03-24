import {
  ArrowLeft,
  Check,
  ClipboardList,
  Copy,
  Download,
  GitCompareArrows,
  Link2,
  X,
} from 'lucide-solid'
import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import { PLAN_ACCENT, PLAN_ACCENT_SUBTLE } from './types'

export const PlanHeader: Component<{
  codename: string | undefined
  copied: boolean
  shareCopied: boolean
  hasDiff?: boolean
  showDiff?: boolean
  onBack: () => void
  onApprove: () => void
  onSendFeedback: () => void
  onCopy: () => void
  onDownload: () => void
  onShare: () => void
  onClose: () => void
  onToggleDiff?: () => void
}> = (props) => {
  return (
    <header
      class="flex items-center gap-3 px-5 flex-shrink-0"
      style={{
        height: '48px',
        background: 'var(--surface-raised)',
        'border-bottom': '1px solid var(--border-subtle)',
      }}
    >
      {/* Left: Back + codename badge */}
      <button
        type="button"
        onClick={() => props.onBack()}
        class="flex items-center gap-1.5 text-[13px] transition-opacity"
        style={{ color: 'var(--text-secondary)', opacity: '0.8' }}
      >
        <ArrowLeft class="w-4 h-4" />
        <span>Back to Chat</span>
      </button>

      <div class="flex items-center gap-2 ml-3">
        <div class="p-1 rounded" style={{ background: PLAN_ACCENT_SUBTLE }}>
          <ClipboardList class="w-4 h-4" style={{ color: PLAN_ACCENT }} />
        </div>
        <Show when={props.codename}>
          <span class="text-[13px] font-bold tracking-wide" style={{ color: PLAN_ACCENT }}>
            {props.codename}
          </span>
        </Show>
        <Show when={props.hasDiff}>
          <button
            type="button"
            onClick={() => props.onToggleDiff?.()}
            class="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[10px] font-medium border transition-colors"
            classList={{
              'text-[#22C55E] border-[rgba(34,197,94,0.3)] bg-[rgba(34,197,94,0.08)]':
                props.showDiff,
              'text-[var(--text-muted)] border-[var(--border-subtle)]': !props.showDiff,
            }}
            title="Toggle diff view"
          >
            <GitCompareArrows class="w-3 h-3" />
            Diff
          </button>
        </Show>
      </div>

      <div class="flex-1" />

      {/* Right: Send Feedback + Approve + icons */}
      <div class="flex items-center gap-2">
        <button
          type="button"
          onClick={() => props.onSendFeedback()}
          class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[12px] font-medium border transition-colors"
          style={{
            color: PLAN_ACCENT,
            'border-color': PLAN_ACCENT,
            background: 'transparent',
          }}
          title="Send feedback from annotations"
        >
          Send Feedback
        </button>
        <button
          type="button"
          onClick={() => props.onApprove()}
          class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[12px] font-medium transition-colors"
          style={{
            color: '#fff',
            background: '#22C55E',
          }}
          title="Approve plan (Ctrl+Enter)"
        >
          Approve
        </button>

        <div class="w-px h-5 mx-1" style={{ background: 'var(--border-subtle)' }} />

        <button
          type="button"
          onClick={() => props.onCopy()}
          class="p-2 rounded-md transition-colors"
          style={{ color: 'var(--text-muted)' }}
          title="Copy as Markdown"
        >
          <Show when={props.copied} fallback={<Copy class="w-4 h-4" />}>
            <Check class="w-4 h-4" style={{ color: '#22C55E' }} />
          </Show>
        </button>
        <button
          type="button"
          onClick={() => props.onDownload()}
          class="p-2 rounded-md transition-colors"
          style={{ color: 'var(--text-muted)' }}
          title="Download (Ctrl+S)"
        >
          <Download class="w-4 h-4" />
        </button>
        <button
          type="button"
          onClick={() => props.onShare()}
          class="p-2 rounded-md transition-colors"
          style={{ color: props.shareCopied ? '#22C55E' : 'var(--text-muted)' }}
          title="Share link"
        >
          <Show when={props.shareCopied} fallback={<Link2 class="w-4 h-4" />}>
            <Check class="w-4 h-4" />
          </Show>
        </button>
        <button
          type="button"
          onClick={() => props.onClose()}
          class="p-2 rounded-md transition-colors"
          style={{ color: 'var(--text-muted)' }}
          title="Close (Esc)"
        >
          <X class="w-4 h-4" />
        </button>
      </div>
    </header>
  )
}
