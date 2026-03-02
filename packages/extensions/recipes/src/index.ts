/**
 * Recipes extension — YAML/JSON-based composable workflows.
 *
 * Discovers recipe files from .ava/recipes/ and ~/.ava/recipes/.
 * Registers a /recipe slash command to list and run recipes.
 */

import type { Disposable, ExtensionAPI, SlashCommand } from '@ava/core-v2/extensions'
import type { ToolContext } from '@ava/core-v2/tools'
import { parseRecipe } from './parser.js'
import { executeRecipe } from './runner.js'
import type { Recipe } from './types.js'

/** Directories to scan for recipe files (relative to project root). */
const PROJECT_RECIPE_DIRS = ['.ava/recipes']

/** Global recipe directory (resolved at runtime). */
const GLOBAL_RECIPE_DIR = '~/.ava/recipes'

/** Supported file extensions. */
const RECIPE_EXTENSIONS = ['.json', '.yaml', '.yml']

export function activate(api: ExtensionAPI): Disposable {
  const recipes: Recipe[] = []
  const disposables: Disposable[] = []

  // Register /recipe slash command
  const recipeCommand: SlashCommand = {
    name: 'recipe',
    description: 'List and run recipes (usage: /recipe [name] [--param=value ...])',
    async execute(args: string, _ctx: ToolContext): Promise<string> {
      const trimmed = args.trim()

      // No args — list recipes
      if (!trimmed) {
        if (recipes.length === 0) {
          return 'No recipes found. Add .json or .yaml files to .ava/recipes/ or ~/.ava/recipes/.'
        }
        const list = recipes
          .map((r) => `  ${r.name}${r.description ? ` — ${r.description}` : ''}`)
          .join('\n')
        return `Available recipes:\n${list}`
      }

      // Parse recipe name and params from args
      const { name, params } = parseCommandArgs(trimmed)
      const recipe = recipes.find((r) => r.name === name)
      if (!recipe) {
        return `Recipe "${name}" not found. Use /recipe to list available recipes.`
      }

      api.emit('recipe:started', { recipe: recipe.name, params })
      try {
        const result = await executeRecipe(recipe, params, api)
        api.emit('recipe:completed', { recipe: recipe.name, result })

        const summary = result.steps
          .map((s) => `  ${s.success ? 'OK' : 'FAIL'} ${s.name} (${s.duration}ms)`)
          .join('\n')
        return `Recipe "${recipe.name}" ${result.success ? 'completed' : 'failed'}:\n${summary}`
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err)
        api.emit('recipe:error', { recipe: recipe.name, error: message })
        return `Recipe "${recipe.name}" failed: ${message}`
      }
    },
  }
  disposables.push(api.registerCommand(recipeCommand))

  // Auto-discover recipes on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      void discoverRecipes(workingDirectory, api).then((discovered) => {
        for (const recipe of discovered) {
          recipes.push(recipe)
        }
        if (discovered.length > 0) {
          api.log.debug(`Discovered ${discovered.length} recipe(s)`)
          api.emit('recipes:discovered', {
            count: discovered.length,
            names: discovered.map((r) => r.name),
          })
        }
      })
    })
  )

  api.log.debug('Recipes extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      recipes.length = 0
    },
  }
}

// ─── Discovery ───────────────────────────────────────────────────────────────

async function discoverRecipes(cwd: string, api: ExtensionAPI): Promise<Recipe[]> {
  const found: Recipe[] = []
  const fs = api.platform.fs

  // Resolve global dir
  const home = process.env.HOME ?? process.env.USERPROFILE ?? '/tmp'
  const globalDir = GLOBAL_RECIPE_DIR.replace('~', home)
  const dirs = [...PROJECT_RECIPE_DIRS.map((d) => `${cwd}/${d}`), globalDir]

  for (const dir of dirs) {
    try {
      const entries = await fs.readDir(dir)
      for (const entry of entries) {
        if (!RECIPE_EXTENSIONS.some((ext) => entry.endsWith(ext))) continue
        const filePath = `${dir}/${entry}`
        try {
          const content = await fs.readFile(filePath)
          const recipe = parseRecipe(content)
          found.push(recipe)
        } catch (err) {
          const message = err instanceof Error ? err.message : String(err)
          api.log.warn(`Failed to parse recipe ${filePath}: ${message}`)
        }
      }
    } catch {
      // Directory does not exist — skip
    }
  }

  return found
}

// ─── Argument Parsing ────────────────────────────────────────────────────────

function parseCommandArgs(input: string): { name: string; params: Record<string, string> } {
  const parts = input.split(/\s+/)
  const name = parts[0] ?? ''
  const params: Record<string, string> = {}

  for (let i = 1; i < parts.length; i++) {
    const part = parts[i]!
    const match = part.match(/^--(\w[\w-]*)=(.*)$/)
    if (match) {
      params[match[1]!] = match[2]!
    }
  }

  return { name, params }
}
