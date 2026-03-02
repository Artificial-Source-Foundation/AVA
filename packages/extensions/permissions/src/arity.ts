/**
 * Arity fingerprinting for bash commands.
 *
 * Maps common commands to their expected argument count, enabling
 * permission fingerprinting: store `["git", "status"]` instead of
 * the full `"git status --verbose --porcelain"`.
 */

import type { BashTokens } from './bash-parser.js'

/**
 * Arity map — number of "meaningful" arguments for common commands.
 *
 * The value represents how many positional arguments define the command's
 * identity. Flags (--verbose, -l) are stripped; only positional subcommands
 * and targets count.
 *
 * 0 = command alone is sufficient (e.g., `ls`, `pwd`)
 * 1 = command + first arg (e.g., `cd <dir>`, `git <subcommand>`)
 * 2 = command + two args (e.g., `grep <pattern> <path>`)
 */
export const ARITY_MAP: Record<string, number> = {
  // Navigation & listing
  ls: 0,
  pwd: 0,
  cd: 1,
  tree: 0,
  du: 0,
  df: 0,
  stat: 1,
  file: 1,

  // File reading
  cat: 1,
  head: 1,
  tail: 1,
  less: 1,
  more: 1,
  wc: 1,
  md5sum: 1,
  sha256sum: 1,

  // File manipulation
  cp: 2,
  mv: 2,
  rm: 1,
  mkdir: 1,
  rmdir: 1,
  touch: 1,
  chmod: 2,
  chown: 2,
  ln: 2,

  // Search
  grep: 2,
  rg: 2,
  find: 1,
  fd: 1,
  ag: 2,
  locate: 1,
  which: 1,
  whereis: 1,

  // Text processing
  sed: 1,
  awk: 1,
  sort: 0,
  uniq: 0,
  cut: 0,
  tr: 2,
  xargs: 1,
  tee: 1,

  // Output
  echo: 1,
  printf: 1,

  // Version control
  git: 1,
  svn: 1,
  hg: 1,

  // Package managers
  npm: 1,
  npx: 1,
  pnpm: 1,
  yarn: 1,
  pip: 1,
  pip3: 1,
  cargo: 1,
  go: 1,
  brew: 1,
  apt: 1,
  'apt-get': 1,

  // Runtimes
  node: 1,
  python: 1,
  python3: 1,
  ruby: 1,
  perl: 1,
  deno: 1,
  bun: 1,
  tsx: 1,

  // Build & test
  make: 1,
  cmake: 1,
  tsc: 0,
  vitest: 1,
  jest: 1,
  eslint: 1,
  biome: 1,
  oxlint: 0,
  prettier: 1,

  // Network
  curl: 1,
  wget: 1,
  ssh: 1,
  scp: 2,
  rsync: 2,
  ping: 1,
  nc: 1,

  // Docker & containers
  docker: 1,
  'docker-compose': 1,
  podman: 1,
  kubectl: 1,

  // System
  env: 0,
  export: 1,
  source: 1,
  kill: 1,
  ps: 0,
  top: 0,
  htop: 0,
  date: 0,
  sleep: 1,
  sudo: 1,
  su: 1,
  whoami: 0,
  id: 0,
  uname: 0,
  hostname: 0,

  // Compression
  tar: 1,
  zip: 1,
  unzip: 1,
  gzip: 1,
  gunzip: 1,

  // Editors (open)
  vim: 1,
  vi: 1,
  nano: 1,
  code: 1,

  // Misc
  diff: 2,
  patch: 1,
  man: 1,
  type: 1,
  alias: 0,
  history: 0,
  clear: 0,
  true: 0,
  false: 0,
  test: 1,
}

/** Check if a token looks like a flag (starts with -). */
function isFlag(token: string): boolean {
  return token.startsWith('-')
}

/**
 * Extract the command prefix up to its arity count.
 *
 * Strips flags and returns [command, ...positionalArgs] limited to the
 * arity for that command. Unknown commands default to arity 1.
 *
 * @example
 * extractCommandPrefix({ command: 'git', args: ['status', '--verbose', '--porcelain'], ... })
 * // => ['git', 'status']
 *
 * @example
 * extractCommandPrefix({ command: 'ls', args: ['-la', '/tmp'], ... })
 * // => ['ls']
 */
export function extractCommandPrefix(tokens: BashTokens): string[] {
  const { command, args } = tokens

  if (!command) return []

  const arity = ARITY_MAP[command] ?? 1
  const prefix = [command]

  if (arity === 0) return prefix

  // Collect positional args (non-flags) up to arity
  let positionalCount = 0
  for (const arg of args) {
    if (isFlag(arg)) continue
    prefix.push(arg)
    positionalCount++
    if (positionalCount >= arity) break
  }

  return prefix
}
