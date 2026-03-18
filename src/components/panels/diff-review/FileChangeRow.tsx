/**
 * File Change Row — single file entry in the diff review panel
 *
 * Shows file name, operation icon, line stats, expand/collapse toggle,
 * and optional "open in editor" button.
 */

import { ChevronDown, ChevronRight, ExternalLink } from 'lucide-solid'
import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { EditorInfo } from '../../../services/ide-integration'
import { openInEditor } from '../../../services/ide-integration'
import type { FileOperation } from '../../../types'
import { DiffReview } from '../DiffReview'
import { getDirectory, getFileName, opColors, opIcons } from './diff-review-helpers'

interface FileChangeRowProps {
  op: FileOperation
  isExpanded: boolean
  editors: EditorInfo[]
  onToggle: () => void
}

export const FileChangeRow: Component<FileChangeRowProps> = (props) => {
  const Icon = () => opIcons[props.op.type]
  const color = () => opColors[props.op.type]

  return (
    <div class="border-b border-[var(--border-subtle)]">
      {/* File header row */}
      <button
        type="button"
        onClick={props.onToggle}
        class="w-full flex items-center gap-2 px-3 py-2 text-left hover:bg-[var(--surface-raised)] transition-colors"
      >
        <Show
          when={props.isExpanded}
          fallback={<ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />}
        >
          <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
        </Show>

        <Dynamic component={Icon()} class="w-3.5 h-3.5 flex-shrink-0" style={{ color: color() }} />

        <div class="flex-1 min-w-0 flex items-center gap-1.5">
          <span class="text-xs font-medium text-[var(--text-primary)] truncate">
            {getFileName(props.op.filePath)}
          </span>
          <span class="text-[10px] text-[var(--text-muted)] truncate">
            {getDirectory(props.op.filePath)}
          </span>
        </div>

        <div class="flex items-center gap-1.5 text-[10px] flex-shrink-0">
          <Show when={props.op.isNew}>
            <span class="px-1.5 py-0.5 bg-[var(--success-subtle)] text-[var(--success)] rounded-full font-medium">
              new
            </span>
          </Show>
          <Show when={props.op.linesAdded}>
            <span class="text-[var(--success)]">+{props.op.linesAdded}</span>
          </Show>
          <Show when={props.op.linesRemoved}>
            <span class="text-[var(--error)]">-{props.op.linesRemoved}</span>
          </Show>
          <Show when={props.editors.length > 0 && props.op.type !== 'delete'}>
            <button
              type="button"
              onClick={(e) => {
                e.stopPropagation()
                void openInEditor(props.editors[0]!.command, props.op.filePath)
              }}
              class="p-0.5 rounded hover:bg-[var(--alpha-white-5)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors cursor-pointer"
              title={`Open in ${props.editors[0]!.name}`}
            >
              <ExternalLink class="w-3 h-3" />
            </button>
          </Show>
        </div>
      </button>

      {/* Expanded diff view */}
      <Show when={props.isExpanded}>
        <div class="px-2 pb-2">
          <DiffReview
            oldContent={props.op.originalContent ?? ''}
            newContent={props.op.newContent ?? ''}
            filename={getFileName(props.op.filePath)}
          />
        </div>
      </Show>
    </div>
  )
}
