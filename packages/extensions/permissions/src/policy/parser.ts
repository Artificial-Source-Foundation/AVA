import TOML from '@iarna/toml'
import YAML from 'yaml'

import type { DeclarativePolicyRule, PolicySource } from '../types.js'
import type { PolicyLoadItem, PolicyParseResult, PolicyRuleInput } from './types.js'

function parseRaw(path: string, content: string): unknown {
  if (path.endsWith('.toml')) return TOML.parse(content)
  return YAML.parse(content)
}

function normalizeRule(
  item: PolicyRuleInput,
  source: PolicySource,
  filePath: string
): DeclarativePolicyRule {
  if (!item.name || !item.tool || !item.decision) {
    throw new Error(`Invalid policy rule in ${filePath}: name/tool/decision are required`)
  }
  if (item.argsPattern) {
    // Validate regex eagerly
    new RegExp(item.argsPattern)
  }
  return {
    name: item.name,
    tool: item.tool,
    decision: item.decision,
    priority: item.priority ?? 0,
    reason: item.reason,
    argsPattern: item.argsPattern,
    paths: item.paths,
    modes: item.modes,
    source,
  }
}

export function parsePolicyFile(file: PolicyLoadItem): PolicyParseResult {
  const warnings: string[] = []
  const raw = parseRaw(file.path, file.content) as { version?: number; rules?: PolicyRuleInput[] }

  if (raw.version !== 1) {
    throw new Error(`Unsupported policy version in ${file.path}: ${String(raw.version)}`)
  }

  const list = raw.rules ?? []
  const rules = list.map((rule) => normalizeRule(rule, file.source, file.path))
  return { rules, warnings }
}
