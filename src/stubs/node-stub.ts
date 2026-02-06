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

// ============================================================================
// child_process
// ============================================================================

export const spawn = notAvailable('spawn')
export const exec = notAvailable('exec')
export const execSync = notAvailable('execSync')
export const execFile = notAvailable('execFile')

// ============================================================================
// fs (sync)
// ============================================================================

export const readFileSync = notAvailable('readFileSync')
export const writeFileSync = notAvailable('writeFileSync')
export const existsSync = () => false
export const statSync = notAvailable('statSync')
export const mkdirSync = notAvailable('mkdirSync')
export const readdirSync = notAvailable('readdirSync')

// ============================================================================
// fs/promises (async)
// ============================================================================

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

// fs namespace for `import * as fs from 'node:fs'`
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

// ============================================================================
// os
// ============================================================================

export const homedir = () => '/home/user'
export const tmpdir = () => '/tmp'
export const platform = () => 'browser'
export const arch = () => 'x64'
export const cpus = () => [{ model: 'browser', speed: 0 }]
export const hostname = () => 'browser'
export const userInfo = () => ({ username: 'user', homedir: '/home/user' })
export const type = () => 'Browser'
export const release = () => '1.0.0'
export const totalmem = () => 0
export const freemem = () => 0
export const EOL = '\n'

// ============================================================================
// crypto
// ============================================================================

export const randomUUID = () => globalThis.crypto.randomUUID()
export const randomBytes = (size: number) => globalThis.crypto.getRandomValues(new Uint8Array(size))

// ============================================================================
// url
// ============================================================================

export const fileURLToPath = (url: string) => url.replace('file://', '')
export const pathToFileURL = (p: string) => `file://${p}`

// ============================================================================
// buffer
// ============================================================================

export const Buffer = globalThis.Buffer || {
  from: (data: string) => new TextEncoder().encode(data),
  alloc: (size: number) => new Uint8Array(size),
}

// ============================================================================
// path
// ============================================================================

export const join = (...paths: string[]) => paths.join('/')
export const resolve = (...paths: string[]) => paths.join('/')
export const dirname = (p: string) => p.split('/').slice(0, -1).join('/')
export const basename = (p: string, ext?: string) => {
  const name = p.split('/').pop() || ''
  if (ext && name.endsWith(ext)) return name.slice(0, -ext.length)
  return name
}
export const extname = (p: string) => {
  const name = p.split('/').pop() || ''
  const dotIndex = name.lastIndexOf('.')
  return dotIndex > 0 ? name.slice(dotIndex) : ''
}
export const relative = (_from: string, to: string) => to
export const isAbsolute = (p: string) => p.startsWith('/')
export const normalize = (p: string) => p
export const sep = '/'

// ============================================================================
// stream
// ============================================================================

export class PassThrough {
  write() {
    return true
  }
  end() {
    return this
  }
  on() {
    return this
  }
  pipe() {
    return this
  }
  destroy() {
    return this
  }
}

export class Readable extends PassThrough {}
export class Writable extends PassThrough {}
export class Transform extends PassThrough {}
export class Duplex extends PassThrough {}

// ============================================================================
// process
// ============================================================================

export const env = {}
export const argv = []
export const cwd = () => '/'
export const exit = notAvailable('process.exit')
export const pid = 0
export const stdout = new PassThrough()
export const stderr = new PassThrough()
export const stdin = new PassThrough()

// ============================================================================
// events
// ============================================================================

export class EventEmitter {
  on() {
    return this
  }
  off() {
    return this
  }
  once() {
    return this
  }
  emit() {
    return false
  }
  addListener() {
    return this
  }
  removeListener() {
    return this
  }
  removeAllListeners() {
    return this
  }
  listeners() {
    return []
  }
  listenerCount() {
    return 0
  }
}

// ============================================================================
// net / http / https
// ============================================================================

export const createServer = notAvailable('createServer')
export const createConnection = notAvailable('createConnection')
export const request = notAvailable('request')
export const get = notAvailable('http.get')

// ============================================================================
// Default export (for `import * as mod` or `import mod from`)
// ============================================================================

export default {
  // child_process
  spawn,
  exec,
  execSync,
  execFile,
  // fs sync
  readFileSync,
  writeFileSync,
  existsSync,
  statSync,
  mkdirSync,
  readdirSync,
  // fs/promises
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
  // path
  join,
  resolve,
  dirname,
  basename,
  extname,
  relative,
  isAbsolute,
  normalize,
  sep,
  // os
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
  // crypto
  randomUUID,
  randomBytes,
  // url
  fileURLToPath,
  pathToFileURL,
  // buffer
  Buffer,
  // stream
  PassThrough,
  Readable,
  Writable,
  Transform,
  Duplex,
  // process
  env,
  argv,
  cwd,
  exit,
  pid,
  stdout,
  stderr,
  stdin,
  // events
  EventEmitter,
  // net/http
  createServer,
  createConnection,
  request,
  get,
}
