import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises'
import { homedir } from 'node:os'
import { join } from 'node:path'
import type { AgentProfile } from './types.js'

function sanitizeName(name: string): string {
  return name
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9-_]+/g, '-')
}

function profilesDir(): string {
  return join(homedir(), '.ava', 'profiles')
}

function profilePath(name: string): string {
  return join(profilesDir(), `${sanitizeName(name)}.json`)
}

export async function saveProfile(profile: AgentProfile): Promise<void> {
  await mkdir(profilesDir(), { recursive: true })
  await writeFile(profilePath(profile.name), JSON.stringify(profile, null, 2), 'utf-8')
}

export async function loadProfile(name: string): Promise<AgentProfile | null> {
  try {
    const data = await readFile(profilePath(name), 'utf-8')
    return JSON.parse(data) as AgentProfile
  } catch {
    return null
  }
}

export async function listProfiles(): Promise<string[]> {
  try {
    await mkdir(profilesDir(), { recursive: true })
    const entries = await readdir(profilesDir(), { withFileTypes: true })
    return entries
      .filter((entry) => entry.isFile() && entry.name.endsWith('.json'))
      .map((entry) => entry.name.replace(/\.json$/, ''))
      .sort((a, b) => a.localeCompare(b))
  } catch {
    return []
  }
}
