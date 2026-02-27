/**
 * Validate Command
 * Run the validation pipeline on files
 *
 * Usage:
 *   ava validate <files...>
 *   ava validate src/index.ts --validators syntax,typescript
 */

import * as path from 'node:path'
import {
  formatReport,
  registerValidator,
  runPipeline,
} from '@ava/extensions/validator/src/pipeline.js'
import type { ValidatorName } from '@ava/extensions/validator/src/types.js'
import {
  lintValidator,
  syntaxValidator,
  typescriptValidator,
} from '@ava/extensions/validator/src/validators.js'

interface ValidateOptions {
  files: string[]
  validators: ValidatorName[]
  cwd: string
  timeout: number
  json: boolean
}

export async function runValidateCommand(args: string[]): Promise<void> {
  const options = parseValidateOptions(args)
  if (!options) {
    printValidateHelp()
    return
  }

  // Resolve file paths to absolute
  const resolvedFiles = options.files.map((f) => path.resolve(options.cwd, f))

  // Register validators
  registerValidator(syntaxValidator)
  registerValidator(typescriptValidator)
  registerValidator(lintValidator)

  // Set up abort controller
  const ac = new AbortController()
  process.on('SIGINT', () => ac.abort())

  try {
    const result = await runPipeline(
      resolvedFiles,
      {
        enabledValidators: options.validators,
        timeout: options.timeout,
        failFast: true,
      },
      ac.signal,
      options.cwd
    )

    if (options.json) {
      console.log(JSON.stringify(result, null, 2))
    } else {
      console.log('')
      console.log(formatReport(result))
      console.log('')
    }

    process.exit(result.passed ? 0 : 1)
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    if (options.json) {
      console.log(JSON.stringify({ error: message }))
    } else {
      console.error(`Validation error: ${message}`)
    }
    process.exit(1)
  }
}

function parseValidateOptions(args: string[]): ValidateOptions | null {
  const files: string[] = []
  let validators: ValidatorName[] = ['syntax', 'typescript', 'lint']
  let cwd = process.cwd()
  let timeout = 30000
  let json = false

  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i]

    if (arg === '--validators') {
      const value = args[++i]
      if (value) {
        validators = value.split(',') as ValidatorName[]
      }
      continue
    }

    if (arg === '--cwd') {
      cwd = args[++i]
      continue
    }

    if (arg === '--timeout') {
      timeout = parseInt(args[++i], 10)
      continue
    }

    if (arg === '--json') {
      json = true
      continue
    }

    if (!arg.startsWith('--')) {
      files.push(arg)
    }
  }

  if (files.length === 0) {
    return null
  }

  return { files, validators, cwd, timeout, json }
}

function printValidateHelp(): void {
  console.log(`
AVA Validate - Run the validation pipeline

USAGE:
  ava validate <files...> [options]

OPTIONS:
  --validators <list>   Comma-separated validators (default: syntax,typescript,lint)
  --cwd <path>          Working directory (default: current)
  --timeout <ms>        Per-validator timeout in ms (default: 30000)
  --json                JSON output

AVAILABLE VALIDATORS:
  syntax                Parse checking (detect syntax errors)
  typescript            Type checking (tsc)
  lint                  Linting (ESLint/Oxlint)

EXAMPLES:
  ava validate src/index.ts
  ava validate packages/core-v2/src/agent/loop.ts
  ava validate src/ --validators syntax,typescript
  ava validate src/index.ts --json
`)
}
