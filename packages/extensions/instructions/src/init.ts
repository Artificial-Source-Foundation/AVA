/**
 * Project rules generator — scans a project and generates a CLAUDE.md template.
 *
 * Detects language, framework, test runner, formatter, and linter from
 * common config files and generates appropriate project instructions.
 */

interface SimpleFS {
  readFile(path: string): Promise<string>
  exists(path: string): Promise<boolean>
}

interface ProjectInfo {
  language: string | null
  framework: string | null
  testRunner: string | null
  formatter: string | null
  linter: string | null
  buildTool: string | null
  packageManager: string | null
}

/** Join a directory path with a filename. */
function joinPath(dir: string, file: string): string {
  return dir.endsWith('/') ? `${dir}${file}` : `${dir}/${file}`
}

/**
 * Detect project characteristics by scanning for config files.
 */
async function detectProject(cwd: string, fs: SimpleFS): Promise<ProjectInfo> {
  const info: ProjectInfo = {
    language: null,
    framework: null,
    testRunner: null,
    formatter: null,
    linter: null,
    buildTool: null,
    packageManager: null,
  }

  // Detect language and package manager
  if (await fs.exists(joinPath(cwd, 'package.json'))) {
    info.language = 'TypeScript/JavaScript'
    info.packageManager = 'npm'

    try {
      const raw = await fs.readFile(joinPath(cwd, 'package.json'))
      const pkg = JSON.parse(raw) as Record<string, unknown>
      const deps = {
        ...(pkg.dependencies as Record<string, string> | undefined),
        ...(pkg.devDependencies as Record<string, string> | undefined),
      }

      // Detect framework
      if (deps.next) info.framework = 'Next.js'
      else if (deps.react) info.framework = 'React'
      else if (deps.vue) info.framework = 'Vue'
      else if (deps.svelte) info.framework = 'Svelte'
      else if (deps['solid-js']) info.framework = 'SolidJS'
      else if (deps.express) info.framework = 'Express'
      else if (deps.fastify) info.framework = 'Fastify'
      else if (deps.angular || deps['@angular/core']) info.framework = 'Angular'

      // Detect test runner
      if (deps.vitest) info.testRunner = 'Vitest'
      else if (deps.jest) info.testRunner = 'Jest'
      else if (deps.mocha) info.testRunner = 'Mocha'

      // Detect package manager
      if (pkg.packageManager && typeof pkg.packageManager === 'string') {
        if (pkg.packageManager.startsWith('pnpm')) info.packageManager = 'pnpm'
        else if (pkg.packageManager.startsWith('yarn')) info.packageManager = 'yarn'
        else if (pkg.packageManager.startsWith('bun')) info.packageManager = 'bun'
      }
    } catch {
      // Invalid JSON — skip
    }
  }

  if (await fs.exists(joinPath(cwd, 'tsconfig.json'))) {
    info.language = 'TypeScript'
  }

  if (await fs.exists(joinPath(cwd, 'Cargo.toml'))) {
    info.language = 'Rust'
    info.buildTool = 'cargo'
    info.testRunner = info.testRunner ?? 'cargo test'
  }

  if (await fs.exists(joinPath(cwd, 'pyproject.toml'))) {
    info.language = info.language ?? 'Python'
    info.buildTool = info.buildTool ?? 'pyproject'

    try {
      const raw = await fs.readFile(joinPath(cwd, 'pyproject.toml'))
      if (raw.includes('pytest')) info.testRunner = info.testRunner ?? 'pytest'
      if (raw.includes('ruff')) info.linter = info.linter ?? 'ruff'
      if (raw.includes('black')) info.formatter = info.formatter ?? 'black'
    } catch {
      // Skip
    }
  }

  if (await fs.exists(joinPath(cwd, 'go.mod'))) {
    info.language = info.language ?? 'Go'
    info.buildTool = info.buildTool ?? 'go'
    info.testRunner = info.testRunner ?? 'go test'
  }

  // Detect formatter
  if (await fs.exists(joinPath(cwd, 'biome.json'))) {
    info.formatter = 'Biome'
    info.linter = info.linter ?? 'Biome'
  }
  if (
    (await fs.exists(joinPath(cwd, '.prettierrc'))) ||
    (await fs.exists(joinPath(cwd, '.prettierrc.json')))
  ) {
    info.formatter = info.formatter ?? 'Prettier'
  }

  // Detect linter
  if (await fs.exists(joinPath(cwd, '.eslintrc'))) {
    info.linter = info.linter ?? 'ESLint'
  }
  if (await fs.exists(joinPath(cwd, '.eslintrc.json'))) {
    info.linter = info.linter ?? 'ESLint'
  }

  // Detect test runner from config files
  if (await fs.exists(joinPath(cwd, 'vitest.config.ts'))) {
    info.testRunner = info.testRunner ?? 'Vitest'
  }
  if (await fs.exists(joinPath(cwd, 'jest.config.ts'))) {
    info.testRunner = info.testRunner ?? 'Jest'
  }
  if (await fs.exists(joinPath(cwd, 'jest.config.js'))) {
    info.testRunner = info.testRunner ?? 'Jest'
  }

  return info
}

/**
 * Generate a CLAUDE.md template with detected project settings.
 */
export async function generateProjectRules(cwd: string, fs: SimpleFS): Promise<string> {
  const info = await detectProject(cwd, fs)

  const lines: string[] = []
  lines.push('# Project Instructions')
  lines.push('')

  // Language & Framework
  if (info.language) {
    lines.push(`## Language`)
    lines.push('')
    lines.push(
      `This project uses **${info.language}**${info.framework ? ` with **${info.framework}**` : ''}.`
    )
    lines.push('')
  }

  // Build
  if (info.packageManager || info.buildTool) {
    lines.push('## Build')
    lines.push('')
    if (info.packageManager) {
      lines.push('```bash')
      lines.push(`${info.packageManager} install  # Install dependencies`)
      lines.push(`${info.packageManager} run build  # Build project`)
      lines.push('```')
    } else if (info.buildTool === 'cargo') {
      lines.push('```bash')
      lines.push('cargo build  # Build project')
      lines.push('```')
    } else if (info.buildTool === 'go') {
      lines.push('```bash')
      lines.push('go build ./...  # Build project')
      lines.push('```')
    }
    lines.push('')
  }

  // Testing
  if (info.testRunner) {
    lines.push(`## Testing (${info.testRunner})`)
    lines.push('')
    switch (info.testRunner) {
      case 'Vitest':
        lines.push('```bash')
        lines.push('npx vitest run  # Run all tests')
        lines.push('npx vitest run <path>  # Run specific test')
        lines.push('```')
        break
      case 'Jest':
        lines.push('```bash')
        lines.push('npx jest  # Run all tests')
        lines.push('npx jest <path>  # Run specific test')
        lines.push('```')
        break
      case 'cargo test':
        lines.push('```bash')
        lines.push('cargo test  # Run all tests')
        lines.push('```')
        break
      case 'go test':
        lines.push('```bash')
        lines.push('go test ./...  # Run all tests')
        lines.push('```')
        break
      case 'pytest':
        lines.push('```bash')
        lines.push('pytest  # Run all tests')
        lines.push('pytest <path>  # Run specific test')
        lines.push('```')
        break
      default:
        lines.push(`Test runner: ${info.testRunner}`)
        break
    }
    lines.push('')
  }

  // Code Quality
  if (info.formatter || info.linter) {
    lines.push('## Code Quality')
    lines.push('')
    if (info.formatter) lines.push(`- **Formatter**: ${info.formatter}`)
    if (info.linter) lines.push(`- **Linter**: ${info.linter}`)
    lines.push('')
  }

  // Code Style section (always include)
  lines.push('## Code Style')
  lines.push('')
  if (info.language === 'TypeScript') {
    lines.push('- Use TypeScript strict mode')
    lines.push('- Prefer `const` over `let`')
    lines.push('- Use explicit return types on exported functions')
  } else if (info.language === 'Rust') {
    lines.push('- Follow Rust conventions (snake_case, clippy clean)')
    lines.push('- Use `Result` types for error handling')
  } else if (info.language === 'Python') {
    lines.push('- Follow PEP 8 style guidelines')
    lines.push('- Use type hints')
  } else if (info.language === 'Go') {
    lines.push('- Follow Go conventions (gofmt, golint clean)')
    lines.push('- Use error wrapping with `fmt.Errorf`')
  } else {
    lines.push('- Follow project conventions')
    lines.push('- Write clean, readable code')
  }
  lines.push('')

  return lines.join('\n')
}
