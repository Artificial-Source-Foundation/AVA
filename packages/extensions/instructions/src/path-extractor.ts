import { dirname, isAbsolute, normalize, resolve } from 'node:path'

const FILE_ARG_KEYS = ['path', 'file_path', 'filePath']

function normalizePath(input: string, cwd: string): string {
  const absolute = isAbsolute(input) ? input : resolve(cwd, input)
  return normalize(absolute)
}

function asFileProbe(path: string): string {
  return path.endsWith('/') ? `${path}.__ava_probe__` : `${path}/.__ava_probe__`
}

function fromDirectArgs(args: Record<string, unknown>, cwd: string): string[] {
  const paths: string[] = []
  for (const key of FILE_ARG_KEYS) {
    const value = args[key]
    if (typeof value === 'string' && value.length > 0) {
      paths.push(normalizePath(value, cwd))
    }
  }
  return paths
}

function extractFromBash(command: string, cwd: string): string[] {
  const paths: string[] = []
  const re = /(?:^|\s)(["']?)(\/[^\s"']+|\.{1,2}\/[^\s"']+)\1/g
  let match = re.exec(command)
  while (match) {
    const token = match[2]
    if (token) paths.push(normalizePath(token, cwd))
    match = re.exec(command)
  }
  return paths
}

export function extractPaths(
  toolName: string,
  args: Record<string, unknown>,
  cwd: string
): string[] {
  if (toolName === 'bash') {
    const command = args.command
    return typeof command === 'string' ? extractFromBash(command, cwd) : []
  }

  if (toolName === 'glob') {
    const path = typeof args.path === 'string' ? args.path : cwd
    const normalized = normalizePath(path, cwd)
    return [asFileProbe(normalized)]
  }

  if (toolName === 'grep') {
    const base = typeof args.path === 'string' ? args.path : cwd
    return [asFileProbe(normalizePath(base, cwd))]
  }

  if (toolName === 'ls') {
    const target = typeof args.path === 'string' ? args.path : cwd
    return [asFileProbe(normalizePath(target, cwd))]
  }

  const direct = fromDirectArgs(args, cwd)
  if (direct.length > 0) return direct

  return []
}

export function toDirectoryKey(path: string): string {
  return dirname(path)
}
