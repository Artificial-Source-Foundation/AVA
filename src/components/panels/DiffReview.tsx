import { Check, RotateCcw, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, For } from 'solid-js'
import { DiffViewer } from '../ui/DiffViewer'

interface DiffReviewProps {
  oldContent: string
  newContent: string
  filename: string
}

interface DiffHunk {
  id: number
  oldContent: string
  newContent: string
}

type HunkDecision = 'accept' | 'reject' | null

function buildHunks(oldContent: string, newContent: string): DiffHunk[] {
  const oldLines = oldContent.split('\n')
  const newLines = newContent.split('\n')
  const maxLen = Math.max(oldLines.length, newLines.length)
  const changedIndexes: number[] = []

  for (let i = 0; i < maxLen; i++) {
    if ((oldLines[i] ?? '') !== (newLines[i] ?? '')) {
      changedIndexes.push(i)
    }
  }

  if (changedIndexes.length === 0) {
    return [{ id: 0, oldContent, newContent }]
  }

  const hunks: DiffHunk[] = []
  let start = changedIndexes[0] ?? 0
  let end = start
  let id = 0

  for (let i = 1; i < changedIndexes.length; i++) {
    const current = changedIndexes[i] ?? end
    if (current - end <= 2) {
      end = current
      continue
    }

    const sliceStart = Math.max(0, start - 1)
    const sliceEnd = end + 2
    hunks.push({
      id,
      oldContent: oldLines.slice(sliceStart, sliceEnd).join('\n'),
      newContent: newLines.slice(sliceStart, sliceEnd).join('\n'),
    })
    id += 1

    start = current
    end = current
  }

  const finalStart = Math.max(0, start - 1)
  const finalEnd = end + 2
  hunks.push({
    id,
    oldContent: oldLines.slice(finalStart, finalEnd).join('\n'),
    newContent: newLines.slice(finalStart, finalEnd).join('\n'),
  })

  return hunks
}

export const DiffReview: Component<DiffReviewProps> = (props) => {
  const hunks = createMemo(() => buildHunks(props.oldContent, props.newContent))
  const [decisions, setDecisions] = createSignal(new Map<number, HunkDecision>())

  const setDecision = (hunkId: number, decision: HunkDecision) => {
    const next = new Map(decisions())
    next.set(hunkId, decision)
    setDecisions(next)
  }

  const acceptAll = () => {
    const next = new Map<number, HunkDecision>()
    for (const hunk of hunks()) next.set(hunk.id, 'accept')
    setDecisions(next)
  }

  const rejectAll = () => {
    const next = new Map<number, HunkDecision>()
    for (const hunk of hunks()) next.set(hunk.id, 'reject')
    setDecisions(next)
  }

  const resetAll = () => setDecisions(new Map())

  return (
    <div class="space-y-2 rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]/60 backdrop-blur-sm p-2">
      <div class="flex items-center justify-between gap-2 px-1">
        <div class="text-[11px] text-[var(--text-secondary)]">{hunks().length} hunk(s)</div>
        <div class="flex items-center gap-1">
          <button
            type="button"
            onClick={acceptAll}
            class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[color-mix(in_srgb,var(--success)_18%,transparent)] text-[var(--success)] border border-[color-mix(in_srgb,var(--success)_35%,transparent)]"
          >
            Accept All
          </button>
          <button
            type="button"
            onClick={rejectAll}
            class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[color-mix(in_srgb,var(--error)_18%,transparent)] text-[var(--error)] border border-[color-mix(in_srgb,var(--error)_35%,transparent)]"
          >
            Reject All
          </button>
          <button
            type="button"
            onClick={resetAll}
            title="Reset decisions"
            class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-muted)] border border-[var(--border-subtle)]"
          >
            <RotateCcw class="w-3 h-3" />
          </button>
        </div>
      </div>

      <For each={hunks()}>
        {(hunk, index) => {
          const decision = () => decisions().get(hunk.id) ?? null
          return (
            <div class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-base)] overflow-hidden">
              <div class="flex items-center justify-between px-2 py-1.5 border-b border-[var(--border-subtle)]">
                <div class="text-[11px] text-[var(--text-secondary)]">Hunk {index() + 1}</div>
                <div class="flex items-center gap-1">
                  <button
                    type="button"
                    onClick={() => setDecision(hunk.id, 'accept')}
                    class="p-1 rounded-[var(--radius-sm)] text-[var(--success)] hover:bg-[color-mix(in_srgb,var(--success)_15%,transparent)]"
                    title="Accept hunk"
                  >
                    <Check class="w-3.5 h-3.5" />
                  </button>
                  <button
                    type="button"
                    onClick={() => setDecision(hunk.id, 'reject')}
                    class="p-1 rounded-[var(--radius-sm)] text-[var(--error)] hover:bg-[color-mix(in_srgb,var(--error)_15%,transparent)]"
                    title="Reject hunk"
                  >
                    <X class="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>

              <DiffViewer
                oldContent={decision() === 'accept' ? hunk.newContent : hunk.oldContent}
                newContent={decision() === 'reject' ? hunk.oldContent : hunk.newContent}
                filename={props.filename}
                mode="unified"
              />
            </div>
          )
        }}
      </For>
    </div>
  )
}
