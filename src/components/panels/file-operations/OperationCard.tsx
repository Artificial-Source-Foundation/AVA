/**
 * Operation Card for File Operations Panel
 *
 * Renders a single file operation with expand/collapse details.
 */

import { Clock, ExternalLink } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { EditorInfo } from '../../../services/ide-integration'
import { openInEditor } from '../../../services/ide-integration'
import type { FileOperation } from '../../../types'
import {
  formatTimestamp,
  getDirectory,
  getFileName,
  operationConfig,
} from './file-operations-helpers'

export interface OperationCardProps {
  operation: FileOperation
  isSelected: boolean
  editors: EditorInfo[]
  onToggle: () => void
}

export const OperationCard: Component<OperationCardProps> = (props) => {
  const config = () => operationConfig[props.operation.type]
  const OperationIcon = () => config().icon

  return (
    <button
      type="button"
      onClick={() => props.onToggle()}
      class={`
        w-full text-left
        p-3
        rounded-[var(--radius-lg)]
        border
        transition-[background-color,border-color,color,transform] duration-[var(--duration-fast)]
        ${
          props.isSelected
            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
            : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
        }
      `}
      style={
        {
          '--operation-accent': config().color,
          '--operation-accent-bg': config().bg,
        } as { '--operation-accent': string; '--operation-accent-bg': string }
      }
    >
      <div class="flex items-start gap-3">
        {/* Operation Icon */}
        <div class="p-2 rounded-[var(--radius-md)] flex-shrink-0 bg-[var(--operation-accent-bg)]">
          <Dynamic component={OperationIcon()} class="w-4 h-4 text-[var(--operation-accent)]" />
        </div>

        {/* Operation Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center justify-between gap-2">
            <span class="text-sm font-medium text-[var(--text-primary)] truncate">
              {getFileName(props.operation.filePath)}
            </span>
            <span class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] font-medium rounded-full flex-shrink-0 bg-[var(--operation-accent-bg)] text-[var(--operation-accent)]">
              {config().label}
            </span>
          </div>

          <p class="text-xs text-[var(--text-muted)] mt-0.5 truncate">
            {getDirectory(props.operation.filePath) || '/'}
          </p>

          {/* Meta info */}
          <div class="flex items-center gap-3 mt-2 text-xs text-[var(--text-muted)]">
            <span class="flex items-center gap-1">
              <Clock class="w-3 h-3" />
              {formatTimestamp(props.operation.timestamp)}
            </span>
            <Show when={props.operation.lines}>
              <span>{props.operation.lines} lines</span>
            </Show>
            <Show
              when={
                props.operation.linesAdded !== undefined ||
                props.operation.linesRemoved !== undefined
              }
            >
              <span class="flex items-center gap-1">
                <Show when={props.operation.linesAdded}>
                  <span class="text-[var(--success)]">+{props.operation.linesAdded}</span>
                </Show>
                <Show when={props.operation.linesRemoved}>
                  <span class="text-[var(--error)]">-{props.operation.linesRemoved}</span>
                </Show>
              </span>
            </Show>
          </div>

          {/* Expanded details */}
          <Show when={props.isSelected}>
            <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
              {/* Agent info */}
              <Show when={props.operation.agentName}>
                <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
                  <span>Agent: {props.operation.agentName}</span>
                </div>
              </Show>

              {/* Full path */}
              <div class="text-xs text-[var(--text-secondary)] p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] font-mono break-all">
                {props.operation.filePath}
              </div>

              {/* Open in editor */}
              <Show when={props.editors.length > 0 && props.operation.type !== 'delete'}>
                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation()
                    void openInEditor(props.editors[0]!.command, props.operation.filePath)
                  }}
                  class="flex items-center gap-1.5 text-xs text-[var(--accent)] hover:underline cursor-pointer"
                >
                  <ExternalLink class="w-3 h-3" />
                  Open in {props.editors[0]!.name}
                </button>
              </Show>

              {/* New file badge */}
              <Show when={props.operation.isNew}>
                <div class="flex items-center gap-1.5">
                  <span class="px-2 py-0.5 text-[10px] font-medium bg-[var(--success-subtle)] text-[var(--success)] rounded-full">
                    New File
                  </span>
                </div>
              </Show>
            </div>
          </Show>
        </div>
      </div>
    </button>
  )
}
