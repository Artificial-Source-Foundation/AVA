import type { DeclarativePolicyRule, PolicySource } from '../types.js'
import type { PolicyLoadItem, PolicyParseResult, PolicyRuleInput } from './types.js'

/**
 * Minimal YAML-subset parser for policy files.
 * Handles the specific structures used in policy files:
 * - Top-level key-value pairs (version: 1)
 * - Array of objects (rules with nested properties)
 */
function parseYaml(content: string): Record<string, unknown> {
  const result: Record<string, unknown> = {}
  const lines = content.split('\n')
  let currentArrayKey = ''
  let currentArray: Record<string, unknown>[] = []
  let currentObj: Record<string, unknown> | null = null

  for (const rawLine of lines) {
    const line = rawLine.replace(/#.*$/, '').trimEnd()
    if (!line.trim()) continue

    // Array item start: "  - key: value"
    const arrayItemMatch = /^\s+-\s+(\w[\w.-]*):\s*(.+)$/.exec(line)
    if (arrayItemMatch && currentArrayKey) {
      // Start new object in current array
      currentObj = { [arrayItemMatch[1]]: parseScalar(arrayItemMatch[2].trim()) }
      currentArray.push(currentObj)
      continue
    }

    // Continuation of array object: "    key: value" (indented, no dash)
    const contMatch = /^\s{4,}(\w[\w.-]*):\s*(.+)$/.exec(line)
    if (contMatch && currentObj) {
      currentObj[contMatch[1]] = parseScalar(contMatch[2].trim())
      continue
    }

    // Top-level key with no value (start of array block): "rules:"
    const blockMatch = /^(\w[\w.-]*):\s*$/.exec(line)
    if (blockMatch) {
      currentArrayKey = blockMatch[1]
      currentArray = []
      currentObj = null
      result[currentArrayKey] = currentArray
      continue
    }

    // Top-level key with inline empty array: "rules: []"
    const emptyArrayMatch = /^(\w[\w.-]*):\s*\[\]\s*$/.exec(line)
    if (emptyArrayMatch) {
      result[emptyArrayMatch[1]] = []
      currentArrayKey = ''
      currentObj = null
      continue
    }

    // Top-level key-value: "version: 1"
    const kvMatch = /^(\w[\w.-]*):\s+(.+)$/.exec(line)
    if (kvMatch) {
      result[kvMatch[1]] = parseScalar(kvMatch[2].trim())
      currentArrayKey = ''
      currentObj = null
    }
  }

  return result
}

/**
 * Minimal TOML-subset parser for policy files.
 * Handles [[array]] sections with key = value pairs.
 */
function parseToml(content: string): Record<string, unknown> {
  const result: Record<string, unknown> = {}
  const lines = content.split('\n')
  let currentSection = ''
  let currentObj: Record<string, unknown> | null = null

  for (const rawLine of lines) {
    const line = rawLine.replace(/#.*$/, '').trimEnd()
    if (!line.trim()) continue

    // Array of tables: [[rules]]
    const arraySection = /^\[\[(\w+)\]\]$/.exec(line)
    if (arraySection) {
      currentSection = arraySection[1]
      currentObj = {}
      if (!Array.isArray(result[currentSection])) {
        result[currentSection] = []
      }
      ;(result[currentSection] as Record<string, unknown>[]).push(currentObj)
      continue
    }

    // Key = value
    const kvMatch = /^(\w[\w.-]*)\s*=\s*(.+)$/.exec(line)
    if (kvMatch) {
      const target = currentObj ?? result
      target[kvMatch[1]] = parseScalar(kvMatch[2].trim())
    }
  }

  return result
}

function parseScalar(val: string): unknown {
  if (val === 'true') return true
  if (val === 'false') return false
  if (val === 'null') return null
  if (/^-?\d+$/.test(val)) return parseInt(val, 10)
  if (/^-?\d+\.\d+$/.test(val)) return parseFloat(val)
  // Strip quotes
  if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
    return val.slice(1, -1)
  }
  return val
}

function parseRaw(path: string, content: string): unknown {
  if (path.endsWith('.json')) return JSON.parse(content)
  if (path.endsWith('.toml')) return parseToml(content)
  return parseYaml(content)
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
