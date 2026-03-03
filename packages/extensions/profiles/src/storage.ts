import { homedir } from 'node:os'
import { join } from 'node:path'
import { getPlatform } from '@ava/core-v2/platform'
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
  const fs = getPlatform().fs
  await fs.mkdir(profilesDir())
  await fs.writeFile(profilePath(profile.name), JSON.stringify(profile, null, 2))
}

export async function loadProfile(name: string): Promise<AgentProfile | null> {
  try {
    const data = await getPlatform().fs.readFile(profilePath(name))
    return JSON.parse(data) as AgentProfile
  } catch {
    return null
  }
}

export async function listProfiles(): Promise<string[]> {
  try {
    const fs = getPlatform().fs
    await fs.mkdir(profilesDir())
    const entries = await fs.readDirWithTypes(profilesDir())
    return entries
      .filter((entry) => entry.isFile && entry.name.endsWith('.json'))
      .map((entry) => entry.name.replace(/\.json$/, ''))
      .sort((a, b) => a.localeCompare(b))
  } catch {
    return []
  }
}
