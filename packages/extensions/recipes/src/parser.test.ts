import { describe, expect, it } from 'vitest'
import { parseRecipe, substituteParams, substituteStepResults } from './parser.js'

describe('parseRecipe', () => {
  it('parses a JSON recipe', () => {
    const json = JSON.stringify({
      name: 'deploy',
      description: 'Deploy the app',
      version: '1.0.0',
      steps: [{ name: 'build', tool: 'bash', args: { command: 'npm run build' } }],
    })

    const recipe = parseRecipe(json)
    expect(recipe.name).toBe('deploy')
    expect(recipe.description).toBe('Deploy the app')
    expect(recipe.version).toBe('1.0.0')
    expect(recipe.steps).toHaveLength(1)
    expect(recipe.steps[0]!.name).toBe('build')
    expect(recipe.steps[0]!.tool).toBe('bash')
  })

  it('parses a recipe with params', () => {
    const json = JSON.stringify({
      name: 'test',
      params: [
        { name: 'target', required: true },
        { name: 'verbose', default: 'false' },
      ],
      steps: [{ name: 'run', tool: 'bash' }],
    })

    const recipe = parseRecipe(json)
    expect(recipe.params).toHaveLength(2)
    expect(recipe.params![0]!.name).toBe('target')
    expect(recipe.params![0]!.required).toBe(true)
    expect(recipe.params![1]!.default).toBe('false')
  })

  it('throws on invalid recipe (missing steps)', () => {
    const json = JSON.stringify({ name: 'bad' })
    expect(() => parseRecipe(json)).toThrow()
  })

  it('throws on invalid recipe (empty steps)', () => {
    const json = JSON.stringify({ name: 'bad', steps: [] })
    expect(() => parseRecipe(json)).toThrow()
  })

  it('throws on invalid recipe (missing name)', () => {
    const json = JSON.stringify({ steps: [{ name: 'x' }] })
    expect(() => parseRecipe(json)).toThrow()
  })

  it('parses a recipe with schedule', () => {
    const json = JSON.stringify({
      name: 'cron-job',
      schedule: '0 * * * *',
      steps: [{ name: 'ping', tool: 'bash' }],
    })

    const recipe = parseRecipe(json)
    expect(recipe.schedule).toBe('0 * * * *')
  })

  it('parses steps with all optional fields', () => {
    const json = JSON.stringify({
      name: 'full',
      steps: [
        {
          name: 'step1',
          tool: 'bash',
          command: '/test',
          goal: 'Build the project',
          args: { dir: './src' },
          parallel: true,
          condition: 'steps.prev.success',
        },
      ],
    })

    const recipe = parseRecipe(json)
    const step = recipe.steps[0]!
    expect(step.tool).toBe('bash')
    expect(step.command).toBe('/test')
    expect(step.goal).toBe('Build the project')
    expect(step.args).toEqual({ dir: './src' })
    expect(step.parallel).toBe(true)
    expect(step.condition).toBe('steps.prev.success')
  })

  it('parses simple YAML-like format', () => {
    const yaml = `name: deploy
description: Deploy the app
steps:
  - name: build
    tool: bash
  - name: test
    tool: bash`

    // The simple YAML parser handles basic cases
    const recipe = parseRecipe(yaml)
    expect(recipe.name).toBe('deploy')
    expect(recipe.steps).toHaveLength(2)
    expect(recipe.steps[0]!.name).toBe('build')
    expect(recipe.steps[0]!.tool).toBe('bash')
    expect(recipe.steps[1]!.name).toBe('test')
  })
})

describe('substituteParams', () => {
  it('substitutes params in step args', () => {
    const recipe = parseRecipe(
      JSON.stringify({
        name: 'test',
        params: [{ name: 'target' }],
        steps: [
          { name: 'build', tool: 'bash', args: { command: 'npm run build --target={{target}}' } },
        ],
      })
    )

    const result = substituteParams(recipe, { target: 'production' })
    expect(result.steps[0]!.args!.command).toBe('npm run build --target=production')
  })

  it('uses default values when param not provided', () => {
    const recipe = parseRecipe(
      JSON.stringify({
        name: 'test',
        params: [{ name: 'env', default: 'staging' }],
        steps: [{ name: 'deploy', tool: 'bash', args: { env: '{{env}}' } }],
      })
    )

    const result = substituteParams(recipe, {})
    expect(result.steps[0]!.args!.env).toBe('staging')
  })

  it('overrides defaults with provided params', () => {
    const recipe = parseRecipe(
      JSON.stringify({
        name: 'test',
        params: [{ name: 'env', default: 'staging' }],
        steps: [{ name: 'deploy', tool: 'bash', args: { env: '{{env}}' } }],
      })
    )

    const result = substituteParams(recipe, { env: 'production' })
    expect(result.steps[0]!.args!.env).toBe('production')
  })

  it('throws on missing required param', () => {
    const recipe = parseRecipe(
      JSON.stringify({
        name: 'test',
        params: [{ name: 'target', required: true }],
        steps: [{ name: 'build', tool: 'bash' }],
      })
    )

    expect(() => substituteParams(recipe, {})).toThrow('Missing required parameter: target')
  })

  it('substitutes params in goal text', () => {
    const recipe = parseRecipe(
      JSON.stringify({
        name: 'test',
        params: [{ name: 'feature' }],
        steps: [{ name: 'implement', goal: 'Implement {{feature}} feature' }],
      })
    )

    const result = substituteParams(recipe, { feature: 'auth' })
    expect(result.steps[0]!.goal).toBe('Implement auth feature')
  })

  it('replaces unknown params with empty string', () => {
    const recipe = parseRecipe(
      JSON.stringify({
        name: 'test',
        steps: [{ name: 'build', tool: 'bash', args: { x: '{{unknown}}' } }],
      })
    )

    const result = substituteParams(recipe, {})
    expect(result.steps[0]!.args!.x).toBe('')
  })
})

describe('substituteStepResults', () => {
  it('substitutes step result references', () => {
    const args = { input: '{{steps.build.result}}' }
    const results = new Map([['build', 'dist/bundle.js']])

    const substituted = substituteStepResults(args, results)
    expect(substituted.input).toBe('dist/bundle.js')
  })

  it('handles multiple references in one value', () => {
    const args = { combined: '{{steps.a.result}} + {{steps.b.result}}' }
    const results = new Map([
      ['a', 'foo'],
      ['b', 'bar'],
    ])

    const substituted = substituteStepResults(args, results)
    expect(substituted.combined).toBe('foo + bar')
  })

  it('replaces missing step results with empty string', () => {
    const args = { input: '{{steps.missing.result}}' }
    const results = new Map<string, string>()

    const substituted = substituteStepResults(args, results)
    expect(substituted.input).toBe('')
  })

  it('preserves values without references', () => {
    const args = { plain: 'no refs here', another: 'static' }
    const results = new Map([['x', 'y']])

    const substituted = substituteStepResults(args, results)
    expect(substituted.plain).toBe('no refs here')
    expect(substituted.another).toBe('static')
  })
})
