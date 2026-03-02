/**
 * Recipe parser — loads and validates recipe files.
 *
 * Supports both JSON format directly and a simple YAML-like format
 * (key: value with indentation-based arrays). Detects format by
 * attempting JSON.parse first, then falling back to YAML-like parsing.
 */

import { validateRecipe } from './schema.js'
import type { Recipe } from './types.js'

/**
 * Parse a recipe from a raw string (JSON or simple YAML).
 * Validates against the zod schema.
 */
export function parseRecipe(content: string): Recipe {
  const trimmed = content.trim()
  const data = tryParseJSON(trimmed) ?? parseSimpleYAML(trimmed)
  return validateRecipe(data) as Recipe
}

/**
 * Substitute {{param}} placeholders in step args with provided param values.
 */
export function substituteParams(recipe: Recipe, params: Record<string, string>): Recipe {
  // Merge defaults with provided params
  const resolved: Record<string, string> = {}
  for (const p of recipe.params ?? []) {
    if (p.default !== undefined) {
      resolved[p.name] = p.default
    }
  }
  Object.assign(resolved, params)

  // Validate required params
  for (const p of recipe.params ?? []) {
    if (p.required && !(p.name in resolved)) {
      throw new Error(`Missing required parameter: ${p.name}`)
    }
  }

  return {
    ...recipe,
    steps: recipe.steps.map((step) => ({
      ...step,
      args: step.args ? substituteRecord(step.args, resolved) : step.args,
      goal: step.goal ? substituteString(step.goal, resolved) : step.goal,
    })),
  }
}

/**
 * Substitute {{steps.stepName.result}} references in step args.
 */
export function substituteStepResults(
  args: Record<string, string>,
  stepResults: Map<string, string>
): Record<string, string> {
  const result: Record<string, string> = {}
  for (const [key, value] of Object.entries(args)) {
    result[key] = value.replace(
      /\{\{steps\.([^.}]+)\.result\}\}/g,
      (_match, stepName: string) => stepResults.get(stepName) ?? ''
    )
  }
  return result
}

// ─── Internal Helpers ────────────────────────────────────────────────────────

function substituteString(template: string, params: Record<string, string>): string {
  return template.replace(/\{\{(\w+)\}\}/g, (_match, key: string) => params[key] ?? '')
}

function substituteRecord(
  record: Record<string, string>,
  params: Record<string, string>
): Record<string, string> {
  const result: Record<string, string> = {}
  for (const [key, value] of Object.entries(record)) {
    result[key] = substituteString(value, params)
  }
  return result
}

function tryParseJSON(content: string): unknown | undefined {
  try {
    return JSON.parse(content) as unknown
  } catch {
    return undefined
  }
}

/**
 * Minimal YAML-like parser for recipe files.
 *
 * Supports:
 * - Top-level key: value pairs
 * - Arrays via "- item" syntax
 * - Nested objects via indentation (one level)
 * - Quoted and unquoted string values
 */
function parseSimpleYAML(content: string): Record<string, unknown> {
  const result: Record<string, unknown> = {}
  const lines = content.split('\n')
  let currentKey = ''
  let currentArray: unknown[] | undefined
  let inArrayOfObjects = false
  let currentObj: Record<string, unknown> | undefined

  for (const rawLine of lines) {
    const line = rawLine.trimEnd()
    if (!line.trim() || line.trim().startsWith('#')) continue

    const indent = line.length - line.trimStart().length
    const trimmed = line.trim()

    // Top-level key: value
    if (indent === 0 && trimmed.includes(':')) {
      flushArrayItem()
      if (currentArray && currentKey) {
        result[currentKey] = currentArray
      }
      currentArray = undefined
      inArrayOfObjects = false
      currentObj = undefined

      const colonIdx = trimmed.indexOf(':')
      currentKey = trimmed.slice(0, colonIdx).trim()
      const rawValue = trimmed.slice(colonIdx + 1).trim()

      if (rawValue) {
        result[currentKey] = parseValue(rawValue)
        currentKey = ''
      } else {
        // Array or nested object follows
        currentArray = []
      }
      continue
    }

    // Array item
    if (trimmed.startsWith('- ') && currentArray) {
      flushArrayItem()
      const itemContent = trimmed.slice(2).trim()

      // Check if it's a key: value (object item in array)
      if (itemContent.includes(':')) {
        inArrayOfObjects = true
        currentObj = {}
        const colonIdx = itemContent.indexOf(':')
        const k = itemContent.slice(0, colonIdx).trim()
        const v = itemContent.slice(colonIdx + 1).trim()
        currentObj[k] = parseValue(v)
      } else {
        inArrayOfObjects = false
        currentObj = undefined
        currentArray.push(parseValue(itemContent))
      }
      continue
    }

    // Continuation of an object within an array item
    if (indent >= 4 && inArrayOfObjects && currentObj && trimmed.includes(':')) {
      const colonIdx = trimmed.indexOf(':')
      const k = trimmed.slice(0, colonIdx).trim()
      const v = trimmed.slice(colonIdx + 1).trim()
      currentObj[k] = parseValue(v)
    }
  }

  // Flush remaining state
  flushArrayItem()
  if (currentArray && currentKey) {
    result[currentKey] = currentArray
  }

  return result

  function flushArrayItem(): void {
    if (inArrayOfObjects && currentObj && currentArray) {
      currentArray.push(currentObj)
      currentObj = undefined
    }
  }
}

function parseValue(raw: string): unknown {
  if (!raw) return ''
  // Remove surrounding quotes
  if ((raw.startsWith('"') && raw.endsWith('"')) || (raw.startsWith("'") && raw.endsWith("'"))) {
    return raw.slice(1, -1)
  }
  if (raw === 'true') return true
  if (raw === 'false') return false
  if (raw === 'null') return null
  const num = Number(raw)
  if (!Number.isNaN(num) && raw !== '') return num
  return raw
}
