import type { IFileSystem } from '@ava/core-v2/platform'

import type { DeclarativePolicyRule } from '../types.js'
import { loadPolicyFiles } from './loader.js'
import { mergePolicyRules } from './merger.js'
import { parsePolicyFile } from './parser.js'

export async function loadDeclarativePolicies(
  fs: IFileSystem,
  cwd: string
): Promise<{ rules: DeclarativePolicyRule[]; warnings: string[] }> {
  const loaded = await loadPolicyFiles(fs, cwd)
  const warnings = [...loaded.warnings]
  const allRules: DeclarativePolicyRule[] = []

  for (const file of loaded.files) {
    try {
      const parsed = parsePolicyFile(file)
      allRules.push(...parsed.rules)
      warnings.push(...parsed.warnings)
    } catch (error) {
      warnings.push(`Failed to parse policy ${file.path}: ${String(error)}`)
    }
  }

  return { rules: mergePolicyRules(allRules), warnings }
}
