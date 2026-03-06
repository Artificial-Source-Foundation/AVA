import { readdir, readFile } from 'node:fs/promises'
import { homedir } from 'node:os'
import { join } from 'node:path'
import type { ToolContext } from '@ava/core-v2/tools'

export interface Recipe {
  name: string
  description: string
  version: string
  author?: string
  steps: RecipeStep[]
}

export interface RecipeStep {
  name: string
  goal: string
  mode?: 'full' | 'light' | 'solo'
  tools?: string[]
  dependsOn?: string[]
}

export interface RecipeResult {
  success: boolean
  steps: Array<{ name: string; status: 'completed' | 'failed'; error?: string }>
}

function parseList(value: string): string[] {
  const trimmed = value.trim()
  if (!trimmed.startsWith('[') || !trimmed.endsWith(']')) return []
  const inner = trimmed.slice(1, -1).trim()
  if (!inner) return []
  return inner
    .split(',')
    .map((item) => item.trim().replace(/^['"]|['"]$/g, ''))
    .filter(Boolean)
}

function parseScalar(value: string): string {
  const trimmed = value.trim()
  return trimmed.replace(/^['"]|['"]$/g, '')
}

/** Parse a recipe from YAML string */
export function parseRecipe(yaml: string): Recipe {
  const lines = yaml.split('\n')
  const root: Partial<Recipe> = { steps: [] }
  let inSteps = false
  let currentStep: Partial<RecipeStep> | null = null

  for (const rawLine of lines) {
    const line = rawLine.trimEnd()
    if (!line.trim() || line.trimStart().startsWith('#')) continue

    if (!inSteps) {
      if (line.trim() === 'steps:') {
        inSteps = true
        continue
      }
      const match = /^([a-zA-Z][\w-]*):\s*(.+)$/.exec(line.trim())
      if (!match) continue
      const key = match[1]!
      const value = parseScalar(match[2]!)
      if (key === 'name' || key === 'description' || key === 'version' || key === 'author') {
        ;(root as Record<string, string>)[key] = value
      }
      continue
    }

    const stepStart = /^\s*-\s+name:\s*(.+)$/.exec(line)
    if (stepStart) {
      if (currentStep) {
        root.steps!.push(currentStep as RecipeStep)
      }
      currentStep = { name: parseScalar(stepStart[1]!) }
      continue
    }

    const fieldMatch = /^\s+([a-zA-Z][\w-]*):\s*(.+)$/.exec(line)
    if (!fieldMatch || !currentStep) continue
    const key = fieldMatch[1]!
    const value = fieldMatch[2]!

    if (key === 'goal') currentStep.goal = parseScalar(value)
    else if (key === 'mode') currentStep.mode = parseScalar(value) as RecipeStep['mode']
    else if (key === 'tools') currentStep.tools = parseList(value)
    else if (key === 'dependsOn') currentStep.dependsOn = parseList(value)
  }

  if (currentStep) {
    root.steps!.push(currentStep as RecipeStep)
  }

  if (!root.name || !root.description || !root.version) {
    throw new Error('Invalid recipe: missing required top-level fields')
  }
  if (!root.steps || root.steps.length === 0) {
    throw new Error('Invalid recipe: steps are required')
  }

  for (const step of root.steps) {
    if (!step.name || !step.goal) {
      throw new Error('Invalid recipe: each step requires name and goal')
    }
  }

  return root as Recipe
}

function orderSteps(steps: RecipeStep[]): RecipeStep[] {
  const byName = new Map(steps.map((s) => [s.name, s]))
  const visiting = new Set<string>()
  const visited = new Set<string>()
  const ordered: RecipeStep[] = []

  const visit = (step: RecipeStep): void => {
    if (visited.has(step.name)) return
    if (visiting.has(step.name)) throw new Error(`Circular dependency at step: ${step.name}`)
    visiting.add(step.name)

    for (const dep of step.dependsOn ?? []) {
      const depStep = byName.get(dep)
      if (!depStep) {
        throw new Error(`Unknown dependency: ${dep}`)
      }
      visit(depStep)
    }

    visiting.delete(step.name)
    visited.add(step.name)
    ordered.push(step)
  }

  for (const step of steps) visit(step)
  return ordered
}

/** Execute a recipe */
export async function executeRecipe(
  recipe: Recipe,
  context: ToolContext,
  onProgress?: (step: string, status: string) => void
): Promise<RecipeResult> {
  const recipeContext = context as ToolContext & {
    runAgentStep?: (step: RecipeStep) => Promise<void>
  }
  const orderedSteps = orderSteps(recipe.steps)
  const result: RecipeResult = { success: true, steps: [] }

  for (const step of orderedSteps) {
    onProgress?.(step.name, 'running')
    try {
      await recipeContext.runAgentStep?.(step)
      result.steps.push({ name: step.name, status: 'completed' })
      onProgress?.(step.name, 'completed')
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      result.success = false
      result.steps.push({ name: step.name, status: 'failed', error: message })
      onProgress?.(step.name, 'failed')
      break
    }
  }

  return result
}

/** Load recipes from ~/.ava/recipes/ and .ava/recipes/ */
export async function discoverRecipes(): Promise<Recipe[]> {
  const roots = [join(homedir(), '.ava', 'recipes'), join(process.cwd(), '.ava', 'recipes')]
  const recipes: Recipe[] = []

  for (const root of roots) {
    let entries: string[] = []
    try {
      entries = await readdir(root)
    } catch {
      continue
    }

    for (const entry of entries) {
      if (!entry.endsWith('.yml') && !entry.endsWith('.yaml')) continue
      try {
        const content = await readFile(join(root, entry), 'utf8')
        recipes.push(parseRecipe(content))
      } catch {
        // Skip invalid recipes
      }
    }
  }

  return recipes
}
