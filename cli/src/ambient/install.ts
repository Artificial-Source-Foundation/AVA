/**
 * Ambient Terminal — shell integration for AVA.
 *
 * Installs a shell function that intercepts `ava @"goal"` invocations
 * and routes them to `ava agent-v2 run` with the current directory and
 * git status injected automatically.
 */

import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'

// ─── Shell Detection ─────────────────────────────────────────────────────────

export type ShellType = 'bash' | 'zsh' | 'fish'

export function getShellType(): ShellType {
  const shell = process.env.SHELL ?? ''
  if (shell.includes('zsh')) return 'zsh'
  if (shell.includes('fish')) return 'fish'
  return 'bash'
}

// ─── Shell Function Generation ───────────────────────────────────────────────

export function generateShellFunction(shell: ShellType): string {
  if (shell === 'fish') {
    return generateFishFunction()
  }
  return generateBashZshFunction()
}

function generateBashZshFunction(): string {
  return `# AVA Ambient Terminal Integration
# Source this file in your .bashrc or .zshrc:
#   source ~/.ava/shell/ava.sh

ava() {
  local real_ava
  real_ava="$(command -v ava 2>/dev/null || echo "npx ava")"

  # If first argument starts with @, route to agent-v2
  if [ -n "$1" ] && [ "\${1#@}" != "$1" ]; then
    local goal="\${1#@}"
    shift

    # Collect remaining args as part of the goal
    if [ $# -gt 0 ]; then
      goal="$goal $*"
    fi

    # Inject cwd and git context
    local extra_args="--cwd $(pwd)"
    if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
      local branch
      branch="$(git branch --show-current 2>/dev/null)"
      if [ -n "$branch" ]; then
        goal="[branch: $branch] $goal"
      fi
    fi

    command $real_ava agent-v2 run "$goal" $extra_args
  else
    command $real_ava "$@"
  fi
}
`
}

function generateFishFunction(): string {
  return `# AVA Ambient Terminal Integration
# Source this file in your config.fish:
#   source ~/.ava/shell/ava.fish

function ava
  set -l real_ava (command -v ava 2>/dev/null; or echo "npx ava")

  # If first argument starts with @, route to agent-v2
  if test (count $argv) -gt 0; and string match -q '@*' -- $argv[1]
    set -l goal (string replace -r '^@' '' -- $argv[1])

    # Collect remaining args as part of the goal
    if test (count $argv) -gt 1
      set goal "$goal "(string join ' ' -- $argv[2..-1])
    end

    # Inject cwd and git context
    set -l extra_args --cwd (pwd)
    if command -q git; and git rev-parse --is-inside-work-tree >/dev/null 2>&1
      set -l branch (git branch --show-current 2>/dev/null)
      if test -n "$branch"
        set goal "[branch: $branch] $goal"
      end
    end

    command $real_ava agent-v2 run "$goal" $extra_args
  else
    command $real_ava $argv
  end
end
`
}

// ─── Install / Uninstall ─────────────────────────────────────────────────────

export function getShellDir(homeDir?: string): string {
  const home = homeDir ?? os.homedir()
  return path.join(home, '.ava', 'shell')
}

function getShellFilename(shell: ShellType): string {
  return shell === 'fish' ? 'ava.fish' : 'ava.sh'
}

function getSourceInstruction(shell: ShellType, shellDir: string): string {
  const file = path.join(shellDir, getShellFilename(shell))
  if (shell === 'fish') {
    return `Add this to your ~/.config/fish/config.fish:\n  source ${file}`
  }
  const rc = shell === 'zsh' ? '~/.zshrc' : '~/.bashrc'
  return `Add this to your ${rc}:\n  source ${file}`
}

export function installAmbient(shell?: ShellType, homeDir?: string): void {
  const resolvedShell = shell ?? getShellType()
  const dir = getShellDir(homeDir)
  const filename = getShellFilename(resolvedShell)
  const filePath = path.join(dir, filename)

  fs.mkdirSync(dir, { recursive: true })
  fs.writeFileSync(filePath, generateShellFunction(resolvedShell), 'utf-8')

  console.log(`Ambient shell function installed to ${filePath}`)
  console.log('')
  console.log(getSourceInstruction(resolvedShell, dir))
  console.log('')
  console.log('After sourcing, use: ava @"your goal here"')
}

export function uninstallAmbient(homeDir?: string): void {
  const dir = getShellDir(homeDir)

  if (!fs.existsSync(dir)) {
    console.log('No ambient shell integration found.')
    return
  }

  const files = fs.readdirSync(dir)
  let removed = 0

  for (const file of files) {
    if (file === 'ava.sh' || file === 'ava.fish') {
      fs.unlinkSync(path.join(dir, file))
      removed++
    }
  }

  if (removed > 0) {
    console.log(`Removed ${removed} shell integration file(s) from ${dir}`)
    console.log('Remember to remove the source line from your shell RC file.')
  } else {
    console.log('No ambient shell integration found.')
  }
}
