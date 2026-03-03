import type { IFileSystem } from '@ava/core-v2/platform'

import type { PolicyLoadItem, PolicyLoadResult } from './types.js'

const PROJECT_POLICY_FILES = ['.ava-policy.yml', '.ava-policy.yaml', '.ava-policy.toml']

async function readFileSafe(
  fs: IFileSystem,
  path: string,
  source: 'project' | 'user'
): Promise<PolicyLoadItem | null> {
  if (!(await fs.exists(path))) return null
  const stat = await fs.stat(path)
  if (!stat || !stat.isFile) return null
  const content = await fs.readFile(path)
  return { path, source, content }
}

export async function loadPolicyFiles(
  fs: IFileSystem,
  cwd: string,
  userHome = process.env.HOME
): Promise<PolicyLoadResult> {
  const files: PolicyLoadItem[] = []
  const warnings: string[] = []

  for (const name of PROJECT_POLICY_FILES) {
    const item = await readFileSafe(fs, `${cwd}/${name}`, 'project')
    if (item) files.push(item)
  }

  const projectDir = `${cwd}/.ava/policies`
  if (await fs.exists(projectDir)) {
    for (const name of await fs.readDir(projectDir)) {
      if (!/\.(ya?ml|toml)$/i.test(name)) continue
      const item = await readFileSafe(fs, `${projectDir}/${name}`, 'project')
      if (item) files.push(item)
    }
  }

  if (userHome) {
    const userDir = `${userHome}/.ava/policies`
    if (await fs.exists(userDir)) {
      for (const name of await fs.readDir(userDir)) {
        if (!/\.(ya?ml|toml)$/i.test(name)) continue
        const item = await readFileSafe(fs, `${userDir}/${name}`, 'user')
        if (item) files.push(item)
      }
    }
  } else {
    warnings.push('HOME is undefined; skipping user policy directory')
  }

  return { files, warnings }
}
