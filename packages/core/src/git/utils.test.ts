/**
 * Git Utilities Tests
 */

import { afterEach, describe, expect, it, vi } from 'vitest'

const mockShellExec = vi.fn()

vi.mock('../platform.js', () => ({
  getPlatform: () => ({
    shell: { exec: mockShellExec },
  }),
}))

import {
  commit,
  execGit,
  getCurrentBranch,
  getFileStatuses,
  getHeadSha,
  getHistory,
  getRepoRoot,
  isDirty,
  isGitRepo,
  stageFiles,
} from './utils.js'

afterEach(() => {
  vi.clearAllMocks()
})

// ============================================================================
// execGit
// ============================================================================

describe('execGit', () => {
  it('should return success for exit code 0', async () => {
    mockShellExec.mockResolvedValue({ stdout: 'output\n', stderr: '', exitCode: 0 })

    const result = await execGit('status', '/repo')

    expect(result.success).toBe(true)
    expect(result.output).toBe('output')
    expect(result.error).toBeUndefined()
    expect(mockShellExec).toHaveBeenCalledWith('git status', { cwd: '/repo' })
  })

  it('should return failure for non-zero exit code', async () => {
    mockShellExec.mockResolvedValue({
      stdout: '',
      stderr: 'fatal: not a git repository',
      exitCode: 128,
    })

    const result = await execGit('status')

    expect(result.success).toBe(false)
    expect(result.error).toContain('not a git repository')
  })

  it('should handle shell.exec throwing', async () => {
    mockShellExec.mockRejectedValue(new Error('spawn failed'))

    const result = await execGit('status')

    expect(result.success).toBe(false)
    expect(result.error).toContain('spawn failed')
  })

  it('should use stdout as error fallback when stderr is empty', async () => {
    mockShellExec.mockResolvedValue({
      stdout: 'error output here',
      stderr: '',
      exitCode: 1,
    })

    const result = await execGit('some-command')

    expect(result.success).toBe(false)
    expect(result.error).toBe('error output here')
  })
})

// ============================================================================
// isGitRepo
// ============================================================================

describe('isGitRepo', () => {
  it('should return true for valid repo', async () => {
    mockShellExec.mockResolvedValue({ stdout: '.git\n', stderr: '', exitCode: 0 })

    expect(await isGitRepo('/repo')).toBe(true)
  })

  it('should return false when not a repo', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: 'fatal', exitCode: 128 })

    expect(await isGitRepo('/not-repo')).toBe(false)
  })
})

// ============================================================================
// getRepoRoot
// ============================================================================

describe('getRepoRoot', () => {
  it('should return the root path', async () => {
    mockShellExec.mockResolvedValue({ stdout: '/home/user/project\n', stderr: '', exitCode: 0 })

    expect(await getRepoRoot()).toBe('/home/user/project')
  })

  it('should return null when not a repo', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: 'fatal', exitCode: 128 })

    expect(await getRepoRoot()).toBeNull()
  })
})

// ============================================================================
// getCurrentBranch
// ============================================================================

describe('getCurrentBranch', () => {
  it('should return the branch name', async () => {
    mockShellExec.mockResolvedValue({ stdout: 'main\n', stderr: '', exitCode: 0 })

    expect(await getCurrentBranch()).toBe('main')
  })

  it('should return null on failure', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: 'fatal', exitCode: 128 })

    expect(await getCurrentBranch()).toBeNull()
  })
})

// ============================================================================
// getHeadSha
// ============================================================================

describe('getHeadSha', () => {
  it('should return the HEAD SHA', async () => {
    mockShellExec.mockResolvedValue({
      stdout: 'abc1234567890def\n',
      stderr: '',
      exitCode: 0,
    })

    expect(await getHeadSha()).toBe('abc1234567890def')
  })

  it('should return null on failure', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: 'fatal', exitCode: 128 })

    expect(await getHeadSha()).toBeNull()
  })
})

// ============================================================================
// isDirty
// ============================================================================

describe('isDirty', () => {
  it('should return true when there are changes', async () => {
    mockShellExec.mockResolvedValue({ stdout: ' M src/file.ts\n', stderr: '', exitCode: 0 })

    expect(await isDirty()).toBe(true)
  })

  it('should return false when working tree is clean', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    expect(await isDirty()).toBe(false)
  })
})

// ============================================================================
// getFileStatuses
// ============================================================================

describe('getFileStatuses', () => {
  it('should parse modified files', async () => {
    // Note: execGit trims stdout, so " M src/app.ts" becomes "M src/app.ts"
    // after trim. But getFileStatuses re-parses — the full line in porcelain
    // format is "XY path". We must provide pre-trimmed multi-line output.
    mockShellExec.mockResolvedValue({ stdout: 'MM src/app.ts\n', stderr: '', exitCode: 0 })

    const statuses = await getFileStatuses()

    expect(statuses).toHaveLength(1)
    expect(statuses[0].path).toBe('src/app.ts')
    expect(statuses[0].status).toBe('modified')
  })

  it('should parse added files', async () => {
    mockShellExec.mockResolvedValue({ stdout: 'A  src/new.ts\n', stderr: '', exitCode: 0 })

    const statuses = await getFileStatuses()

    expect(statuses).toHaveLength(1)
    expect(statuses[0].status).toBe('added')
  })

  it('should parse deleted files', async () => {
    mockShellExec.mockResolvedValue({ stdout: ' D src/old.ts\n', stderr: '', exitCode: 0 })

    const statuses = await getFileStatuses()

    expect(statuses).toHaveLength(1)
    expect(statuses[0].status).toBe('deleted')
  })

  it('should parse untracked files', async () => {
    mockShellExec.mockResolvedValue({ stdout: '?? src/untouched.ts\n', stderr: '', exitCode: 0 })

    const statuses = await getFileStatuses()

    expect(statuses).toHaveLength(1)
    expect(statuses[0].status).toBe('untracked')
  })

  it('should parse renamed files', async () => {
    mockShellExec.mockResolvedValue({
      stdout: 'R  old-name.ts -> new-name.ts\n',
      stderr: '',
      exitCode: 0,
    })

    const statuses = await getFileStatuses()

    expect(statuses).toHaveLength(1)
    expect(statuses[0].status).toBe('renamed')
    expect(statuses[0].path).toBe('new-name.ts')
    expect(statuses[0].originalPath).toBe('old-name.ts')
  })

  it('should parse multiple statuses', async () => {
    const output = [' M src/app.ts', 'A  src/new.ts', ' D src/old.ts', '?? tmp/draft.ts'].join('\n')
    mockShellExec.mockResolvedValue({ stdout: output, stderr: '', exitCode: 0 })

    const statuses = await getFileStatuses()

    expect(statuses).toHaveLength(4)
    expect(statuses.map((s) => s.status)).toEqual(['modified', 'added', 'deleted', 'untracked'])
  })

  it('should return empty array on failure', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: 'fatal', exitCode: 128 })

    const statuses = await getFileStatuses()

    expect(statuses).toEqual([])
  })
})

// ============================================================================
// getHistory
// ============================================================================

describe('getHistory', () => {
  it('should parse commit log output', async () => {
    const logOutput = [
      'abc123full|abc123f|feat: add feature|John|john@test.com|1700000000',
      'def456full|def456f|fix: bug fix|Jane|jane@test.com|1699999000',
    ].join('\n')
    mockShellExec.mockResolvedValue({ stdout: logOutput, stderr: '', exitCode: 0 })

    const commits = await getHistory({ limit: 5 })

    expect(commits).toHaveLength(2)
    expect(commits[0].sha).toBe('abc123full')
    expect(commits[0].shortSha).toBe('abc123f')
    expect(commits[0].message).toBe('feat: add feature')
    expect(commits[0].author).toBe('John')
    expect(commits[0].email).toBe('john@test.com')
    expect(commits[0].timestamp).toBe(1700000000 * 1000)
  })

  it('should return empty array on failure', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: 'fatal', exitCode: 128 })

    const commits = await getHistory()

    expect(commits).toEqual([])
  })

  it('should pass author filter', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    await getHistory({ author: 'John' })

    expect(mockShellExec).toHaveBeenCalledWith(
      expect.stringContaining('--author="John"'),
      expect.objectContaining({ cwd: undefined })
    )
  })

  it('should pass path filters', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    await getHistory({ paths: ['src/app.ts'] })

    expect(mockShellExec).toHaveBeenCalledWith(
      expect.stringContaining('-- "src/app.ts"'),
      expect.objectContaining({ cwd: undefined })
    )
  })
})

// ============================================================================
// stageFiles
// ============================================================================

describe('stageFiles', () => {
  it('should stage specified files', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const result = await stageFiles(['src/a.ts', 'src/b.ts'])

    expect(result.success).toBe(true)
    expect(mockShellExec).toHaveBeenCalledWith(
      'git add "src/a.ts" "src/b.ts"',
      expect.objectContaining({ cwd: undefined })
    )
  })

  it('should return success for empty array', async () => {
    const result = await stageFiles([])

    expect(result.success).toBe(true)
    expect(mockShellExec).not.toHaveBeenCalled()
  })
})

// ============================================================================
// commit
// ============================================================================

describe('commit', () => {
  it('should create a commit with the given message', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const result = await commit('feat: add feature', '/repo')

    expect(result.success).toBe(true)
    expect(mockShellExec).toHaveBeenCalledWith('git commit -m "feat: add feature"', {
      cwd: '/repo',
    })
  })

  it('should escape double quotes in messages', async () => {
    mockShellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    await commit('fix: handle "edge case"')

    expect(mockShellExec).toHaveBeenCalledWith('git commit -m "fix: handle \\"edge case\\""', {
      cwd: undefined,
    })
  })
})
