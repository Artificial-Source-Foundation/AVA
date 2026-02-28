import { GitBranch, GitPullRequest, RefreshCw, Upload } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, onMount, Show } from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { formatCost } from '../../lib/cost'
import {
  listBranches,
  openCreatePr,
  pullCurrentBranch,
  pushCurrentBranch,
  switchBranch,
} from '../../services/git-actions'
import { logError, logInfo } from '../../services/logger'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { UsageDetailsDialog } from './UsageDetailsDialog'

type GitAction = 'refresh' | 'pull' | 'push' | 'pr' | 'switch'

export const GitControlStrip: Component = () => {
  const { currentProject, refreshGitInfo } = useProject()
  const { contextUsage, sessionTokenStats, messages } = useSession()
  const { isStreaming } = useChat()

  const [branches, setBranches] = createSignal<string[]>([])
  const [selectedBranch, setSelectedBranch] = createSignal('')
  const [activeAction, setActiveAction] = createSignal<GitAction | null>(null)
  const [status, setStatus] = createSignal<string | null>(null)
  const [error, setError] = createSignal<string | null>(null)
  const [showUsageDetails, setShowUsageDetails] = createSignal(false)
  const [initialTab, setInitialTab] = createSignal<'session' | 'project'>('session')
  let statusTimer: ReturnType<typeof setTimeout> | undefined

  // Listen for command palette project stats trigger
  onMount(() => {
    const handler = () => {
      setInitialTab('project')
      setShowUsageDetails(true)
    }
    window.addEventListener('ava:open-project-stats', handler)
    onCleanup(() => window.removeEventListener('ava:open-project-stats', handler))
  })

  const projectDir = createMemo(() => currentProject()?.directory)
  const branch = createMemo(() => currentProject()?.git?.branch || '')
  const isGitProject = createMemo(
    () => !!currentProject() && !!currentProject()?.git && projectDir() && projectDir() !== '~'
  )

  const isBusy = createMemo(() => activeAction() !== null)

  const runGitAction = async (action: GitAction, fn: () => Promise<void>) => {
    if (statusTimer) {
      clearTimeout(statusTimer)
      statusTimer = undefined
    }
    setError(null)
    setStatus(null)
    setActiveAction(action)
    try {
      await fn()
      await refreshGitInfo()
      setStatus('Done')
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Git action failed'
      setError(message)
      logError('git-strip', 'Git action failed', { action, message })
    } finally {
      setActiveAction(null)
      statusTimer = setTimeout(() => {
        setStatus(null)
        setError(null)
      }, 3000)
    }
  }

  const loadBranches = async () => {
    const dir = projectDir()
    if (!dir || dir === '~') {
      setBranches([])
      return
    }

    await runGitAction('refresh', async () => {
      const allBranches = await listBranches(dir)
      setBranches(allBranches)
      setSelectedBranch(branch())
      logInfo('git-strip', 'Loaded git branches', { count: allBranches.length })
    })
  }

  const handleSwitchBranch = async () => {
    const dir = projectDir()
    const target = selectedBranch()
    if (!dir || dir === '~' || !target || target === branch()) return

    await runGitAction('switch', async () => {
      await switchBranch(dir, target)
      logInfo('git-strip', 'Switched branch', { to: target })
    })
  }

  const handlePull = async () => {
    const dir = projectDir()
    if (!dir || dir === '~') return
    await runGitAction('pull', async () => {
      await pullCurrentBranch(dir)
      logInfo('git-strip', 'Pulled branch', { branch: branch() })
    })
  }

  const handlePush = async () => {
    const dir = projectDir()
    if (!dir || dir === '~') return
    await runGitAction('push', async () => {
      await pushCurrentBranch(dir)
      logInfo('git-strip', 'Pushed branch', { branch: branch() })
    })
  }

  const handleCreatePr = async () => {
    const dir = projectDir()
    const currentBranch = branch()
    if (!dir || dir === '~' || !currentBranch) return
    await runGitAction('pr', async () => {
      await openCreatePr(dir, currentBranch)
      logInfo('git-strip', 'Opened create PR URL', { branch: currentBranch })
    })
  }

  onMount(() => {
    void loadBranches()
  })

  onCleanup(() => {
    if (statusTimer) clearTimeout(statusTimer)
  })

  return (
    <Show when={isGitProject()}>
      <div class="border-t border-[var(--border-subtle)] density-section-px py-2 bg-[var(--surface-raised)]">
        <div class="flex flex-col gap-1.5">
          <div class="flex items-center justify-between gap-2">
            <div class="flex items-center gap-2 min-w-0">
              <div class="flex items-center gap-1 text-[10px] text-[var(--text-muted)]">
                <GitBranch class="w-3 h-3" />
                <span class="truncate max-w-28" title={branch()}>
                  {branch() || 'no-branch'}
                </span>
              </div>

              <select
                value={selectedBranch()}
                onChange={(e) => setSelectedBranch(e.currentTarget.value)}
                class="px-2 py-1 text-[10px] bg-[var(--input-background)] border border-[var(--input-border)] rounded-[var(--radius-sm)] text-[var(--text-primary)] min-w-28"
                disabled={isBusy()}
                aria-label="Switch branch"
              >
                <For each={branches()}>{(b) => <option value={b}>{b}</option>}</For>
              </select>

              <button
                type="button"
                onClick={() => void handleSwitchBranch()}
                disabled={isBusy() || !selectedBranch() || selectedBranch() === branch()}
                class="px-2 py-1 text-[10px] rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
              >
                Switch
              </button>

              <button
                type="button"
                onClick={() => void loadBranches()}
                disabled={isBusy()}
                class="p-1 rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
                title="Refresh branches"
                aria-label="Refresh branches"
              >
                <RefreshCw
                  class={`w-3 h-3 ${activeAction() === 'refresh' ? 'animate-spin' : ''}`}
                />
              </button>
            </div>

            <div class="flex items-center gap-1.5">
              <button
                type="button"
                onClick={() => void handlePull()}
                disabled={isBusy()}
                class="px-2 py-1 text-[10px] rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
                aria-label="Pull latest changes"
              >
                Pull
              </button>
              <button
                type="button"
                onClick={() => void handlePush()}
                disabled={isBusy()}
                class="inline-flex items-center gap-1 px-2 py-1 text-[10px] rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
                aria-label="Push current branch"
              >
                <Upload class="w-3 h-3" />
                Push
              </button>
              <button
                type="button"
                onClick={() => void handleCreatePr()}
                disabled={isBusy()}
                class="inline-flex items-center gap-1 px-2 py-1 text-[10px] rounded-[var(--radius-sm)] border border-[var(--accent-muted)] text-[var(--accent)] disabled:opacity-50"
                aria-label="Create pull request"
              >
                <GitPullRequest class="w-3 h-3" />
                PR
              </button>
            </div>
          </div>

          <div class="flex items-center justify-between text-[10px] text-[var(--text-tertiary)]">
            <div class="flex items-center gap-2">
              <Show when={sessionTokenStats().totalCost > 0}>
                <span>{formatCost(sessionTokenStats().totalCost)}</span>
              </Show>
              <Show when={isStreaming()}>
                <span class="text-[var(--accent)] animate-pulse">Streaming</span>
              </Show>
              <button
                type="button"
                onClick={() => setShowUsageDetails(true)}
                class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-1.5 py-0.5 text-[10px] text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
              >
                Usage details
              </button>
            </div>

            <Show when={status() || error()}>
              <span class={error() ? 'text-[var(--error)]' : 'text-[var(--success)]'}>
                {error() || status()}
              </span>
            </Show>
          </div>
        </div>
      </div>

      <UsageDetailsDialog
        open={showUsageDetails()}
        onClose={() => {
          setShowUsageDetails(false)
          setInitialTab('session')
        }}
        contextUsage={contextUsage()}
        sessionTokenStats={sessionTokenStats()}
        messages={messages()}
        projectId={currentProject()?.id}
        initialTab={initialTab()}
      />
    </Show>
  )
}
