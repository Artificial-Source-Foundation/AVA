import { execFile } from 'node:child_process'
import { constants as fsConstants } from 'node:fs'
import { access, mkdir, readFile, writeFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'
import type { LLMClient } from '@ava/core-v2/llm'

export type LLMClientFactory = () => LLMClient

export interface ProviderManifest {
  name: string
  package: string
  version?: string
  factory: string
  models: string[]
  authEnvVar?: string
}

const execFileAsync = promisify(execFile)
const PROVIDERS_ROOT = path.join(os.homedir(), '.ava', 'providers')
const MANIFEST_CACHE = path.join(PROVIDERS_ROOT, 'manifest.json')

const bundledLoaders: Record<string, () => Promise<LLMClientFactory>> = {
  openai: async () => {
    const mod = await import('../openai/src/client.js')
    const OpenAIClientCtor = mod.OpenAIClient as unknown as new () => LLMClient
    return () => new OpenAIClientCtor()
  },
  anthropic: async () => {
    const mod = await import('../anthropic/src/client.js')
    const AnthropicClientCtor = mod.AnthropicClient as unknown as new () => LLMClient
    return () => new AnthropicClientCtor()
  },
  openrouter: async () => {
    const mod = await import('../openrouter/src/client.js')
    const OpenRouterClientCtor = mod.OpenRouterClient as unknown as new () => LLMClient
    return () => new OpenRouterClientCtor()
  },
  google: async () => {
    const mod = await import('../google/src/client.js')
    const client = mod.GoogleClient as unknown as LLMClient
    return () => client
  },
}

async function pathExists(filePath: string): Promise<boolean> {
  try {
    await access(filePath, fsConstants.F_OK)
    return true
  } catch {
    return false
  }
}

async function readCachedManifest(): Promise<ProviderManifest[]> {
  if (!(await pathExists(MANIFEST_CACHE))) {
    return []
  }

  try {
    const raw = await readFile(MANIFEST_CACHE, 'utf8')
    const parsed = JSON.parse(raw) as unknown
    if (!Array.isArray(parsed)) {
      return []
    }
    return parsed.filter(
      (item): item is ProviderManifest =>
        typeof item === 'object' &&
        item !== null &&
        typeof (item as ProviderManifest).name === 'string' &&
        typeof (item as ProviderManifest).package === 'string' &&
        typeof (item as ProviderManifest).factory === 'string'
    )
  } catch {
    return []
  }
}

async function writeCachedManifest(registry: ProviderManifest[]): Promise<void> {
  await mkdir(PROVIDERS_ROOT, { recursive: true })
  await writeFile(MANIFEST_CACHE, JSON.stringify(registry, null, 2), 'utf8')
}

async function installProviderPackage(manifest: ProviderManifest): Promise<void> {
  await mkdir(PROVIDERS_ROOT, { recursive: true })
  const pkg = manifest.version ? `${manifest.package}@${manifest.version}` : manifest.package
  await execFileAsync('npm', ['install', '--yes', '--no-audit', '--no-fund', pkg], {
    cwd: PROVIDERS_ROOT,
  })
}

async function resolveFactoryFromManifest(manifest: ProviderManifest): Promise<LLMClientFactory> {
  const modulePath = path.join(PROVIDERS_ROOT, 'node_modules', manifest.package)
  const moduleUrl = pathToFileURL(modulePath).href
  const mod = (await import(moduleUrl)) as Record<string, unknown>
  const factory = mod[manifest.factory]

  if (typeof factory !== 'function') {
    throw new Error(
      `Provider '${manifest.name}' did not export factory '${manifest.factory}' from '${manifest.package}'.`
    )
  }

  return factory as LLMClientFactory
}

function autoInstallEnabled(): boolean {
  return process.env.AVA_PROVIDERS_AUTO_INSTALL === 'true'
}

export async function loadProvider(
  name: string,
  registry: ProviderManifest[] = []
): Promise<LLMClientFactory> {
  const bundledLoader = bundledLoaders[name]
  if (bundledLoader) {
    return bundledLoader()
  }

  const cached = await readCachedManifest()
  const combined = [...registry, ...cached]
  const manifest = combined.find((item) => item.name === name)
  if (!manifest) {
    throw new Error(`No provider manifest found for '${name}'.`)
  }

  const packagePath = path.join(PROVIDERS_ROOT, 'node_modules', manifest.package)
  const installed = await pathExists(packagePath)

  if (!installed) {
    if (!autoInstallEnabled()) {
      throw new Error(
        `Provider '${name}' is not installed. Enable auto-install with providers.autoInstall (AVA_PROVIDERS_AUTO_INSTALL=true).`
      )
    }
    await installProviderPackage(manifest)
  }

  await writeCachedManifest(
    [...combined.filter((item) => item.name !== manifest.name), manifest].sort((a, b) =>
      a.name.localeCompare(b.name)
    )
  )

  return resolveFactoryFromManifest(manifest)
}

export async function fetchRegistry(url?: string): Promise<ProviderManifest[]> {
  if (!url) {
    return []
  }

  const response = await fetch(url)
  if (!response.ok) {
    throw new Error(`Failed to fetch provider registry (${response.status}).`)
  }

  const parsed = (await response.json()) as unknown
  if (!Array.isArray(parsed)) {
    throw new Error('Provider registry response must be an array.')
  }

  return parsed.filter(
    (item): item is ProviderManifest =>
      typeof item === 'object' &&
      item !== null &&
      typeof (item as ProviderManifest).name === 'string' &&
      typeof (item as ProviderManifest).package === 'string' &&
      typeof (item as ProviderManifest).factory === 'string' &&
      Array.isArray((item as ProviderManifest).models)
  )
}
