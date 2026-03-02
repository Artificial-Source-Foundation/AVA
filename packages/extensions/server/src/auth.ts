/**
 * Server token authentication — generate, validate, persist tokens.
 *
 * Tokens stored in ~/.ava/server-tokens.json.
 */

import type { IncomingMessage } from 'node:http'
import { homedir } from 'node:os'

const TOKEN_LENGTH = 48

interface TokenEntry {
  token: string
  name: string
  createdAt: number
}

interface TokenStore {
  tokens: TokenEntry[]
}

function resolveTokenPath(configPath: string): string {
  return configPath.replace('~', homedir())
}

/** Generate a cryptographically random token. */
export function generateToken(name: string = 'default'): TokenEntry {
  const bytes = new Uint8Array(TOKEN_LENGTH)
  crypto.getRandomValues(bytes)
  const token = `ava_${Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')}`
  return { token, name, createdAt: Date.now() }
}

/** Load token store from disk. */
export async function loadTokens(tokenFile: string): Promise<TokenStore> {
  const path = resolveTokenPath(tokenFile)
  try {
    const { readFile } = await import('node:fs/promises')
    const data = await readFile(path, 'utf-8')
    return JSON.parse(data) as TokenStore
  } catch {
    return { tokens: [] }
  }
}

/** Save token store to disk. */
export async function saveTokens(tokenFile: string, store: TokenStore): Promise<void> {
  const path = resolveTokenPath(tokenFile)
  const { mkdir, writeFile } = await import('node:fs/promises')
  const { dirname } = await import('node:path')
  await mkdir(dirname(path), { recursive: true })
  await writeFile(path, JSON.stringify(store, null, 2), 'utf-8')
}

/** Validate a bearer token from an HTTP request. */
export async function validateRequest(req: IncomingMessage, tokenFile: string): Promise<boolean> {
  const authHeader = req.headers.authorization
  if (!authHeader?.startsWith('Bearer ')) return false

  const token = authHeader.slice(7)
  const store = await loadTokens(tokenFile)
  return store.tokens.some((entry) => entry.token === token)
}

/** Add a token and persist to disk. */
export async function addToken(tokenFile: string, name?: string): Promise<TokenEntry> {
  const entry = generateToken(name)
  const store = await loadTokens(tokenFile)
  store.tokens.push(entry)
  await saveTokens(tokenFile, store)
  return entry
}

/** Remove a token by value. */
export async function removeToken(tokenFile: string, token: string): Promise<boolean> {
  const store = await loadTokens(tokenFile)
  const before = store.tokens.length
  store.tokens = store.tokens.filter((e) => e.token !== token)
  if (store.tokens.length === before) return false
  await saveTokens(tokenFile, store)
  return true
}
