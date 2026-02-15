import { openUrl } from '@tauri-apps/plugin-opener'
import { Command } from '@tauri-apps/plugin-shell'

interface GitCommandResult {
  code: number
  stdout: string
  stderr: string
}

async function runGit(cwd: string, args: string[]): Promise<GitCommandResult> {
  const command = Command.create('git', args, { cwd })
  const output = await command.execute()
  return {
    code: output.code ?? 1,
    stdout: output.stdout.trim(),
    stderr: output.stderr.trim(),
  }
}

async function runGitStrict(cwd: string, args: string[]): Promise<string> {
  const result = await runGit(cwd, args)
  if (result.code !== 0) {
    throw new Error(result.stderr || `git ${args.join(' ')} failed`)
  }
  return result.stdout
}

export async function listBranches(cwd: string): Promise<string[]> {
  const output = await runGitStrict(cwd, ['branch', '--format=%(refname:short)'])
  return output
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean)
}

export async function switchBranch(cwd: string, branch: string): Promise<void> {
  await runGitStrict(cwd, ['checkout', '--', branch])
}

export async function pullCurrentBranch(cwd: string): Promise<void> {
  await runGitStrict(cwd, ['pull', '--ff-only'])
}

export async function pushCurrentBranch(cwd: string): Promise<void> {
  await runGitStrict(cwd, ['push'])
}

function remoteToHttps(remote: string): string | null {
  if (remote.startsWith('http://') || remote.startsWith('https://')) {
    return remote.replace(/\.git$/, '')
  }

  const match = remote.match(/^git@([^:]+):(.+)$/)
  if (!match) return null
  const [, host, path] = match
  return `https://${host}/${path.replace(/\.git$/, '')}`
}

export async function openCreatePr(cwd: string, branch: string): Promise<void> {
  const remote = await runGitStrict(cwd, ['remote', 'get-url', 'origin'])
  const remoteHttp = remoteToHttps(remote)
  if (!remoteHttp) {
    throw new Error('Unsupported git remote format for PR creation')
  }

  const url = `${remoteHttp}/compare/${encodeURIComponent(branch)}?expand=1`
  await openUrl(url)
}
