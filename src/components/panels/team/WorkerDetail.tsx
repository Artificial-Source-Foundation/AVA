/**
 * Worker Detail Panel
 *
 * Shows details for a selected team member: token usage,
 * files changed, and delegation/tool call history.
 * Renders as a split right panel inside the TeamPanel.
 */

import { Clock, FileCode2, Hash, Zap } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { TEAM_DOMAINS, type TeamMember } from '../../../types/team.js'

// ============================================================================
// Sub-components
// ============================================================================

const TokenUsageSection: Component<{ input: number; output: number }> = (props) => {
  const formatTokens = (n: number) => {
    if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}M`
    if (n >= 1_000) return `${(n / 1_000).toFixed(1)}K`
    return String(n)
  }

  return (
    <div class="px-3 py-2 border-b border-[var(--border-subtle)]">
      <div class="flex items-center gap-1.5 mb-1.5">
        <Zap class="w-3 h-3 text-[var(--text-muted)]" />
        <span class="font-[var(--font-ui-mono)] text-[10px] font-medium text-[var(--text-secondary)] uppercase tracking-wider">
          Tokens
        </span>
      </div>
      <div class="flex gap-3">
        <div class="flex-1 rounded-[var(--radius-md)] bg-[var(--alpha-white-3)] px-2 py-1.5">
          <div class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)]">Input</div>
          <div class="font-[var(--font-ui-mono)] text-[13px] font-semibold text-[var(--text-primary)]">
            {formatTokens(props.input)}
          </div>
        </div>
        <div class="flex-1 rounded-[var(--radius-md)] bg-[var(--alpha-white-3)] px-2 py-1.5">
          <div class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)]">Output</div>
          <div class="font-[var(--font-ui-mono)] text-[13px] font-semibold text-[var(--text-primary)]">
            {formatTokens(props.output)}
          </div>
        </div>
      </div>
    </div>
  )
}

const FilesSection: Component<{ files: string[] }> = (props) => (
  <div class="px-3 py-2 border-b border-[var(--border-subtle)]">
    <div class="flex items-center gap-1.5 mb-1.5">
      <FileCode2 class="w-3 h-3 text-[var(--text-muted)]" />
      <span class="font-[var(--font-ui-mono)] text-[10px] font-medium text-[var(--text-secondary)] uppercase tracking-wider">
        Files Changed ({props.files.length})
      </span>
    </div>
    <div class="max-h-[120px] overflow-y-auto scrollbar-none space-y-0.5">
      <For each={props.files}>
        {(file) => (
          <div class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-secondary)] truncate px-1.5 py-0.5 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-3)]">
            {file}
          </div>
        )}
      </For>
    </div>
  </div>
)

const ToolCallsSection: Component<{ member: TeamMember }> = (props) => {
  const formatDuration = (ms: number) => {
    if (ms >= 1000) return `${(ms / 1000).toFixed(1)}s`
    return `${ms}ms`
  }

  return (
    <div class="px-3 py-2">
      <div class="flex items-center gap-1.5 mb-1.5">
        <Hash class="w-3 h-3 text-[var(--text-muted)]" />
        <span class="font-[var(--font-ui-mono)] text-[10px] font-medium text-[var(--text-secondary)] uppercase tracking-wider">
          Tool Calls ({props.member.toolCalls.length})
        </span>
      </div>
      <div class="max-h-[160px] overflow-y-auto scrollbar-none space-y-0.5">
        <For each={props.member.toolCalls}>
          {(tc) => (
            <div class="flex items-center gap-2 px-1.5 py-1 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-3)]">
              <span
                class="w-1.5 h-1.5 rounded-full flex-shrink-0"
                style={{
                  background:
                    tc.status === 'success'
                      ? 'var(--success)'
                      : tc.status === 'error'
                        ? 'var(--error)'
                        : 'var(--accent)',
                }}
              />
              <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-primary)] flex-1 truncate">
                {tc.name}
              </span>
              <Show when={tc.durationMs !== undefined}>
                <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)] flex-shrink-0">
                  <Clock class="w-2.5 h-2.5 inline mr-0.5" />
                  {formatDuration(tc.durationMs!)}
                </span>
              </Show>
            </div>
          )}
        </For>
      </div>
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

export const WorkerDetail: Component<{ member: TeamMember }> = (props) => {
  const config = () => TEAM_DOMAINS[props.member.domain]

  return (
    <div class="flex flex-col h-full border-l border-[var(--border-subtle)] bg-[var(--surface)]">
      {/* Header */}
      <div class="flex items-center gap-2 px-3 py-2 border-b border-[var(--border-subtle)]">
        <span class="w-2 h-2 rounded-[2px] flex-shrink-0" style={{ background: config().color }} />
        <div class="flex-1 min-w-0">
          <div class="font-[var(--font-ui-mono)] text-[11px] font-semibold text-[var(--text-primary)] truncate">
            {props.member.name}
          </div>
          <div class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)]">
            {props.member.model}
          </div>
        </div>
      </div>

      {/* Content */}
      <div class="flex-1 overflow-y-auto scrollbar-none">
        <Show when={props.member.tokenUsage}>
          <TokenUsageSection
            input={props.member.tokenUsage!.input}
            output={props.member.tokenUsage!.output}
          />
        </Show>

        <Show when={props.member.filesChanged && props.member.filesChanged.length > 0}>
          <FilesSection files={props.member.filesChanged!} />
        </Show>

        <Show when={props.member.toolCalls.length > 0}>
          <ToolCallsSection member={props.member} />
        </Show>

        <Show
          when={
            !props.member.tokenUsage &&
            (!props.member.filesChanged || props.member.filesChanged.length === 0) &&
            props.member.toolCalls.length === 0
          }
        >
          <div class="flex items-center justify-center h-full text-center p-4">
            <p class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)]">
              No activity yet
            </p>
          </div>
        </Show>
      </div>
    </div>
  )
}
