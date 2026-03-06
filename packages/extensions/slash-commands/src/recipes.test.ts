import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import type { ToolContext } from '@ava/core-v2/tools'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { discoverRecipes, executeRecipe, parseRecipe } from './recipes.js'

const VALID_RECIPE = `
name: add-feature
description: Standard workflow
version: "1.0"
steps:
  - name: research
    goal: "Research files"
  - name: implement
    goal: "Implement feature"
    dependsOn: [research]
`

describe('parseRecipe', () => {
  it('parses valid YAML recipe', () => {
    const recipe = parseRecipe(VALID_RECIPE)
    expect(recipe.name).toBe('add-feature')
    expect(recipe.steps).toHaveLength(2)
    expect(recipe.steps[1]?.dependsOn).toEqual(['research'])
  })

  it('rejects invalid recipe missing required fields', () => {
    expect(() => parseRecipe('name: x\nsteps:\n  - name: a\n')).toThrow()
  })
})

describe('executeRecipe', () => {
  it('runs steps in dependency order and calls step runner', async () => {
    const recipe = parseRecipe(VALID_RECIPE)
    const calls: string[] = []
    const context = {
      sessionId: 's1',
      workingDirectory: '/tmp',
      signal: new AbortController().signal,
      runAgentStep: async (step: { name: string }) => {
        calls.push(step.name)
      },
    } as ToolContext & { runAgentStep: (step: { name: string }) => Promise<void> }

    const result = await executeRecipe(recipe, context)
    expect(result.success).toBe(true)
    expect(calls).toEqual(['research', 'implement'])
  })
})

describe('discoverRecipes', () => {
  let tempDir = ''
  let originalCwd = ''

  beforeEach(async () => {
    originalCwd = process.cwd()
    tempDir = await mkdtemp(join(tmpdir(), 'ava-recipes-'))
    process.chdir(tempDir)
  })

  afterEach(async () => {
    process.chdir(originalCwd)
    if (tempDir) {
      await rm(tempDir, { recursive: true, force: true })
    }
    vi.restoreAllMocks()
  })

  it('discovers recipes from project filesystem', async () => {
    const projectRecipes = join(tempDir, '.ava', 'recipes')
    await mkdir(projectRecipes, { recursive: true })
    await writeFile(join(projectRecipes, 'feature.yaml'), VALID_RECIPE, 'utf8')

    const recipes = await discoverRecipes()
    expect(recipes.some((r) => r.name === 'add-feature')).toBe(true)
  })
})
