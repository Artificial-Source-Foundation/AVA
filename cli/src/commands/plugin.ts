import { spawn } from 'node:child_process'
import * as fs from 'node:fs/promises'
import * as path from 'node:path'
import { createPluginSource, createReadme } from './plugin-templates'

interface PluginInitOptions {
  pluginName: string
  targetDirectory: string
  force: boolean
}

interface PluginRunnerOptions {
  pluginName: string
  targetDirectory: string
}

export async function runPluginCommand(args: string[]): Promise<void> {
  const subcommand = args[0]

  switch (subcommand) {
    case 'init': {
      const options = parseInitOptions(args.slice(1))
      if (!options) {
        printPluginHelp()
        return
      }

      await scaffoldPlugin(options)
      break
    }

    case 'dev': {
      const options = parseRunnerOptions(args.slice(1))
      if (!options) {
        printPluginHelp()
        return
      }

      await runPluginScript(options, 'dev')
      break
    }

    case 'test': {
      const options = parseRunnerOptions(args.slice(1))
      if (!options) {
        printPluginHelp()
        return
      }

      await runPluginScript(options, 'test')
      break
    }

    default:
      printPluginHelp()
  }
}

function parseInitOptions(args: string[]): PluginInitOptions | null {
  const pluginName = args[0]
  if (!pluginName) {
    return null
  }

  let targetDirectory = process.cwd()
  let force = false

  for (let index = 1; index < args.length; index += 1) {
    const flag = args[index]

    if (flag === '--force') {
      force = true
      continue
    }

    if (flag === '--dir') {
      const value = args[index + 1]
      if (!value) {
        return null
      }

      targetDirectory = path.resolve(value)
      index += 1
      continue
    }

    return null
  }

  return {
    pluginName,
    targetDirectory,
    force,
  }
}

function parseRunnerOptions(args: string[]): PluginRunnerOptions | null {
  const pluginName = args[0]
  if (!pluginName) {
    return null
  }

  let targetDirectory = process.cwd()

  for (let index = 1; index < args.length; index += 1) {
    const flag = args[index]
    if (flag === '--dir') {
      const value = args[index + 1]
      if (!value) {
        return null
      }

      targetDirectory = path.resolve(value)
      index += 1
      continue
    }

    return null
  }

  return {
    pluginName,
    targetDirectory,
  }
}

async function scaffoldPlugin(options: PluginInitOptions): Promise<void> {
  const safeName = toKebabCase(options.pluginName)
  if (!safeName) {
    throw new Error('Plugin name must include at least one alphanumeric character.')
  }

  const pluginDirectory = path.join(options.targetDirectory, safeName)

  await ensureDirectory(pluginDirectory, options.force)

  const files: Array<{ relativePath: string; content: string }> = [
    {
      relativePath: 'package.json',
      content: JSON.stringify(
        {
          name: `@ava-plugin/${safeName}`,
          version: '0.1.0',
          private: true,
          type: 'module',
          description: `${options.pluginName} plugin for AVA`,
          scripts: {
            build: 'tsc -p tsconfig.json',
            dev: 'tsc -w -p tsconfig.json',
            test: 'vitest run',
          },
          devDependencies: {
            typescript: '^5.9.2',
            vitest: '^3.2.4',
          },
        },
        null,
        2
      ),
    },
    {
      relativePath: 'tsconfig.json',
      content: JSON.stringify(
        {
          compilerOptions: {
            target: 'ES2022',
            module: 'NodeNext',
            moduleResolution: 'NodeNext',
            declaration: true,
            outDir: 'dist',
            strict: true,
            esModuleInterop: true,
            skipLibCheck: true,
          },
          include: ['src/**/*.ts'],
        },
        null,
        2
      ),
    },
    {
      relativePath: 'README.md',
      content: createReadme(options.pluginName, safeName),
    },
    {
      relativePath: 'src/index.ts',
      content: createPluginSource(options.pluginName, safeName),
    },
  ]

  for (const file of files) {
    const outputPath = path.join(pluginDirectory, file.relativePath)
    await fs.mkdir(path.dirname(outputPath), { recursive: true })
    await fs.writeFile(outputPath, `${file.content.trimEnd()}\n`, 'utf-8')
  }

  console.log('')
  console.log(`✅ Created plugin scaffold: ${pluginDirectory}`)
  console.log('')
  console.log('Next steps:')
  console.log(`  1. cd ${safeName}`)
  console.log('  2. pnpm install')
  console.log('  3. pnpm run build')
  console.log('')
  console.log('Docs: docs/plugins/PLUGIN_TEMPLATE.md')
}

async function ensureDirectory(directoryPath: string, force: boolean): Promise<void> {
  try {
    const entries = await fs.readdir(directoryPath)
    if (entries.length > 0 && !force) {
      throw new Error(`Target directory is not empty: ${directoryPath}. Use --force to continue.`)
    }
  } catch (error) {
    const isMissing = (error as NodeJS.ErrnoException).code === 'ENOENT'
    if (!isMissing) {
      throw error
    }
  }

  await fs.mkdir(directoryPath, { recursive: true })
}

async function runPluginScript(
  options: PluginRunnerOptions,
  script: 'dev' | 'test'
): Promise<void> {
  const safeName = toKebabCase(options.pluginName)
  if (!safeName) {
    throw new Error('Plugin name must include at least one alphanumeric character.')
  }

  const pluginDirectory = path.join(options.targetDirectory, safeName)
  const packageJsonPath = path.join(pluginDirectory, 'package.json')

  try {
    await fs.access(packageJsonPath)
  } catch {
    throw new Error(
      `Plugin not found at ${pluginDirectory}. Run "ava plugin init ${options.pluginName}" first.`
    )
  }

  await new Promise<void>((resolve, reject) => {
    const child = spawn('pnpm', ['run', script], {
      cwd: pluginDirectory,
      stdio: 'inherit',
    })

    child.on('error', reject)
    child.on('close', (code) => {
      if (code === 0) {
        resolve()
        return
      }

      reject(new Error(`Plugin ${script} failed with exit code ${code ?? 'unknown'}.`))
    })
  })
}

function toKebabCase(value: string): string {
  return value
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
}

function printPluginHelp(): void {
  console.log(`
AVA Plugin - Plugin development commands

USAGE:
  ava plugin <command> [args]

COMMANDS:
  init <name> [--dir <path>] [--force]   Create a new plugin scaffold
  dev <name> [--dir <path>]              Run plugin dev/watch script
  test <name> [--dir <path>]             Run plugin tests

EXAMPLES:
  ava plugin init my-quality-plugin
  ava plugin init my-quality-plugin --dir ./plugins
  ava plugin init my-quality-plugin --dir ./plugins --force
  ava plugin dev my-quality-plugin --dir ./plugins
  ava plugin test my-quality-plugin --dir ./plugins
`)
}
