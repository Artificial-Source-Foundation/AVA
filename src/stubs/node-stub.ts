/**
 * Node.js module stubs for browser compatibility
 *
 * These stubs replace Node.js-only modules when bundled for the browser.
 * The actual implementations only run in Node.js context (CLI).
 * All async stubs return rejected promises so errors surface clearly.
 */

const notAvailable = (name: string) => () => {
  throw new Error(`${name} is not available in browser context`)
}
const notAvailableAsync = (name: string) => async () => {
  throw new Error(`${name} is not available in browser context`)
}

// child_process
export const spawn = notAvailable('spawn')
export const exec = notAvailable('exec')
export const execSync = notAvailable('execSync')
export const execFile = notAvailable('execFile')

// fs (sync)
export const readFileSync = notAvailable('readFileSync')
export const writeFileSync = notAvailable('writeFileSync')
export const existsSync = (): boolean => false
export const statSync = notAvailable('statSync')
export const mkdirSync = notAvailable('mkdirSync')
export const readdirSync = notAvailable('readdirSync')

// fs/promises (async)
export const readFile = notAvailableAsync('readFile')
export const writeFile = notAvailableAsync('writeFile')
export const mkdir = notAvailableAsync('mkdir')
export const readdir = notAvailableAsync('readdir')
export const stat = notAvailableAsync('stat')
export const rm = notAvailableAsync('rm')
export const cp = notAvailableAsync('cp')
export const symlink = notAvailableAsync('symlink')
export const rename = notAvailableAsync('rename')
export const unlink = notAvailableAsync('unlink')
export const access = notAvailableAsync('access')
export const copyFile = notAvailableAsync('copyFile')
export const promises = {
  readFile,
  writeFile,
  mkdir,
  readdir,
  stat,
  rm,
  cp,
  symlink,
  rename,
  unlink,
  access,
  copyFile,
}

// os
export const homedir = (): string => '/home/user'
export const tmpdir = (): string => '/tmp'
export const platform = (): string => 'browser'
export const arch = (): string => 'x64'
export const cpus = (): Array<{ model: string; speed: number }> => [{ model: 'browser', speed: 0 }]
export const hostname = (): string => 'browser'
export const userInfo = (): { username: string; homedir: string } => ({
  username: 'user',
  homedir: '/home/user',
})
export const type = (): string => 'Browser'
export const release = (): string => '1.0.0'
export const totalmem = (): number => 0
export const freemem = (): number => 0
export const EOL = '\n'

// crypto
export const randomUUID = (): string => globalThis.crypto.randomUUID()
export const randomBytes = (size: number): Uint8Array =>
  globalThis.crypto.getRandomValues(new Uint8Array(size))

// url
export const fileURLToPath = (url: string): string => url.replace('file://', '')
export const pathToFileURL = (p: string): string => `file://${p}`

// buffer
export const Buffer = globalThis.Buffer || {
  from: (data: string) => new TextEncoder().encode(data),
  alloc: (size: number) => new Uint8Array(size),
}

// path
export const join = (...paths: string[]): string => paths.join('/')
export const resolve = (...paths: string[]): string => paths.join('/')
export const dirname = (p: string): string => p.split('/').slice(0, -1).join('/')
export const basename = (p: string, ext?: string): string => {
  const name = p.split('/').pop() || ''
  return ext && name.endsWith(ext) ? name.slice(0, -ext.length) : name
}
export const extname = (p: string): string => {
  const name = p.split('/').pop() || ''
  const dotIndex = name.lastIndexOf('.')
  return dotIndex > 0 ? name.slice(dotIndex) : ''
}
export const relative = (_from: string, to: string): string => to
export const isAbsolute = (p: string): boolean => p.startsWith('/')
export const normalize = (p: string): string => p
export const parse = (
  p: string
): { root: string; dir: string; base: string; ext: string; name: string } => {
  const base = p.split('/').pop() || ''
  const dotIndex = base.lastIndexOf('.')
  const ext = dotIndex > 0 ? base.slice(dotIndex) : ''
  const name = dotIndex > 0 ? base.slice(0, dotIndex) : base
  const dir = p.split('/').slice(0, -1).join('/')
  return { root: p.startsWith('/') ? '/' : '', dir, base, ext, name }
}
export const format = (obj: {
  dir?: string
  base?: string
  name?: string
  ext?: string
}): string => {
  const base = obj.base || `${obj.name || ''}${obj.ext || ''}`
  return obj.dir ? `${obj.dir}/${base}` : base
}
export const sep = '/'

// stream
export class PassThrough {
  write(): boolean {
    return true
  }
  end(): this {
    return this
  }
  on(): this {
    return this
  }
  pipe(): this {
    return this
  }
  destroy(): this {
    return this
  }
}
export class Readable extends PassThrough {}
export class Writable extends PassThrough {}
export class Transform extends PassThrough {}
export class Duplex extends PassThrough {}

// process
export const env = {}
export const argv: string[] = []
export const cwd = (): string => '/'
export const exit = notAvailable('process.exit')
export const pid = 0
export const stdout = new PassThrough()
export const stderr = new PassThrough()
export const stdin = new PassThrough()

// events
export class EventEmitter {
  on(): this {
    return this
  }
  off(): this {
    return this
  }
  once(): this {
    return this
  }
  emit(): boolean {
    return false
  }
  addListener(): this {
    return this
  }
  removeListener(): this {
    return this
  }
  removeAllListeners(): this {
    return this
  }
  listeners(): never[] {
    return []
  }
  listenerCount(): number {
    return 0
  }
}

// net / http / https
export const createServer = notAvailable('createServer')
export const createConnection = notAvailable('createConnection')
export const request = notAvailable('request')
export const get = notAvailable('http.get')

// Default export for `import mod from 'node:fs'` style
const _all: Record<string, unknown> = {
  spawn,
  exec,
  execSync,
  execFile,
  readFileSync,
  writeFileSync,
  existsSync,
  statSync,
  mkdirSync,
  readdirSync,
  readFile,
  writeFile,
  mkdir,
  readdir,
  stat,
  rm,
  cp,
  symlink,
  rename,
  unlink,
  access,
  copyFile,
  promises,
  join,
  resolve,
  dirname,
  basename,
  extname,
  relative,
  isAbsolute,
  normalize,
  parse,
  format,
  sep,
  homedir,
  tmpdir,
  platform,
  arch,
  cpus,
  hostname,
  userInfo,
  type,
  release,
  totalmem,
  freemem,
  EOL,
  randomUUID,
  randomBytes,
  fileURLToPath,
  pathToFileURL,
  Buffer,
  PassThrough,
  Readable,
  Writable,
  Transform,
  Duplex,
  env,
  argv,
  cwd,
  exit,
  pid,
  stdout,
  stderr,
  stdin,
  EventEmitter,
  createServer,
  createConnection,
  request,
  get,
}
export default _all
