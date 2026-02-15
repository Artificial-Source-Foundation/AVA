/**
 * Git Auto-Commit
 * Automatically commits file changes made by AI tools.
 * Enables undo of AI edits via git revert.
 */

import { getSettingsManager } from '../config/manager.js'
import type { GitConfig, GitResult } from './types.js'
import { commit, execGit, getHistory, isGitRepo, stageFiles } from './utils.js'

/** File-modifying tool names that should trigger auto-commit */
const FILE_MODIFYING_TOOLS = new Set([
  'write_file',
  'create_file',
  'edit',
  'multiedit',
  'apply_patch',
  'delete_file',
])

/** Paths that should never be auto-committed */
const EXCLUDED_PATTERNS = ['.env', '.env.local', '.env.production', 'credentials', '.secret']

/**
 * Get git config from the core settings manager.
 * Falls back to defaults if settings manager is unavailable.
 */
function getGitConfig(): GitConfig {
  try {
    return getSettingsManager().get('git')
  } catch {
    return { enabled: true, autoCommit: false, branchPrefix: 'ava/', messagePrefix: '[ava]' }
  }
}

/**
 * Check if a file path should be excluded from auto-commit.
 */
function isExcluded(filePath: string): boolean {
  const lower = filePath.toLowerCase()
  return EXCLUDED_PATTERNS.some((p) => lower.includes(p))
}

/**
 * Generate a commit message for auto-committed changes.
 */
function generateCommitMessage(toolName: string, filePaths: string[], prefix: string): string {
  const action =
    toolName === 'delete_file' ? 'delete' : toolName === 'create_file' ? 'create' : 'update'
  const shortPaths = filePaths.map((p) => {
    // Show only filename or last 2 segments for readability
    const parts = p.replace(/\\/g, '/').split('/')
    return parts.length > 2 ? parts.slice(-2).join('/') : parts.join('/')
  })

  if (shortPaths.length === 1) {
    return `${prefix} ${action}: ${shortPaths[0]}`
  }
  if (shortPaths.length <= 3) {
    return `${prefix} ${action}: ${shortPaths.join(', ')}`
  }
  return `${prefix} ${action}: ${shortPaths[0]} and ${shortPaths.length - 1} more files`
}

/**
 * Auto-commit file changes if git auto-commit is enabled.
 *
 * Called from the tool registry after a successful file-modifying tool execution.
 * Only commits if:
 * 1. Git integration is enabled in settings
 * 2. autoCommit is enabled in settings
 * 3. The working directory is a git repo
 * 4. The tool is a file-modifying tool
 * 5. File paths are not excluded (e.g. .env files)
 *
 * @param toolName - Name of the tool that was executed
 * @param filePaths - File paths that were modified
 * @param cwd - Working directory (project root)
 * @returns GitResult if a commit was made, null otherwise
 */
export async function autoCommitIfEnabled(
  toolName: string,
  filePaths: string[],
  cwd: string
): Promise<GitResult | null> {
  // Only handle file-modifying tools
  if (!FILE_MODIFYING_TOOLS.has(toolName)) return null

  // Check settings
  const config = getGitConfig()
  if (!config.enabled || !config.autoCommit) return null

  // Filter out excluded paths
  const committable = filePaths.filter((p) => !isExcluded(p))
  if (committable.length === 0) return null

  // Check if we're in a git repo
  if (!(await isGitRepo(cwd))) return null

  try {
    // Stage the modified files
    const stageResult = await stageFiles(committable, cwd)
    if (!stageResult.success) {
      console.warn('[git-auto-commit] Failed to stage files:', stageResult.error)
      return null
    }

    // Generate and create the commit
    const message = generateCommitMessage(toolName, committable, config.messagePrefix)
    const result = await commit(message, cwd)

    if (result.success) {
      console.info(`[git-auto-commit] ${message}`)
    } else {
      // Commit might fail if there are no actual changes (e.g. file written with same content)
      // Unstage silently in that case
      await execGit(`reset HEAD ${committable.map((p) => `"${p}"`).join(' ')}`, cwd)
    }

    return result
  } catch (err) {
    console.warn('[git-auto-commit] Error:', err)
    return null
  }
}

/**
 * Undo the last auto-commit made by AVA.
 *
 * Finds the most recent commit with the AVA prefix and reverts it.
 * Only reverts commits that match the configured message prefix.
 *
 * @param cwd - Working directory (project root)
 * @returns GitResult with the revert outcome
 */
export async function undoLastAutoCommit(cwd: string): Promise<GitResult> {
  if (!(await isGitRepo(cwd))) {
    return { success: false, output: '', error: 'Not a git repository' }
  }

  const config = getGitConfig()
  const history = await getHistory({ limit: 20 }, cwd)

  // Find the most recent AVA auto-commit
  const avaCommit = history.find((c) => c.message.startsWith(config.messagePrefix))
  if (!avaCommit) {
    return { success: false, output: '', error: 'No AVA auto-commit found to undo' }
  }

  // Revert the commit (creates a new revert commit)
  const result = await execGit(`revert --no-edit ${avaCommit.sha}`, cwd)
  if (result.success) {
    console.info(`[git-auto-commit] Reverted: ${avaCommit.message}`)
  }

  return result
}

/**
 * Get the count of recent auto-commits for display in the UI.
 */
export async function getAutoCommitCount(cwd: string): Promise<number> {
  if (!(await isGitRepo(cwd))) return 0

  const config = getGitConfig()
  const history = await getHistory({ limit: 50 }, cwd)
  return history.filter((c) => c.message.startsWith(config.messagePrefix)).length
}
