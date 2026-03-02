import { describe, expect, it } from 'vitest'
import { generateProjectRules } from './init.js'

/**
 * Simple in-memory FS for testing.
 */
function createTestFS(files: Record<string, string> = {}) {
  return {
    async readFile(path: string): Promise<string> {
      const content = files[path]
      if (content === undefined) throw new Error(`ENOENT: ${path}`)
      return content
    },
    async exists(path: string): Promise<boolean> {
      return path in files
    },
  }
}

describe('generateProjectRules', () => {
  it('generates basic template for empty project', async () => {
    const fs = createTestFS({})
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('# Project Instructions')
    expect(result).toContain('## Code Style')
  })

  it('detects TypeScript from tsconfig.json', async () => {
    const fs = createTestFS({
      '/project/tsconfig.json': '{}',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('TypeScript')
    expect(result).toContain('strict mode')
  })

  it('detects React framework from package.json', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        dependencies: { react: '^18.0.0' },
      }),
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('React')
  })

  it('detects Next.js framework', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        dependencies: { next: '^14.0.0', react: '^18.0.0' },
      }),
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Next.js')
  })

  it('detects SolidJS framework', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        dependencies: { 'solid-js': '^1.0.0' },
      }),
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('SolidJS')
  })

  it('detects Vitest test runner from package.json', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        devDependencies: { vitest: '^1.0.0' },
      }),
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Vitest')
    expect(result).toContain('npx vitest run')
  })

  it('detects Vitest from config file', async () => {
    const fs = createTestFS({
      '/project/vitest.config.ts': 'export default {}',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Vitest')
  })

  it('detects Jest test runner', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        devDependencies: { jest: '^29.0.0' },
      }),
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Jest')
    expect(result).toContain('npx jest')
  })

  it('detects Biome formatter', async () => {
    const fs = createTestFS({
      '/project/biome.json': '{}',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Biome')
    expect(result).toContain('Formatter')
  })

  it('detects Prettier formatter', async () => {
    const fs = createTestFS({
      '/project/.prettierrc': '{}',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Prettier')
  })

  it('detects ESLint linter', async () => {
    const fs = createTestFS({
      '/project/.eslintrc.json': '{}',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('ESLint')
  })

  it('detects Rust project from Cargo.toml', async () => {
    const fs = createTestFS({
      '/project/Cargo.toml': '[package]\nname = "my-crate"',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Rust')
    expect(result).toContain('cargo')
    expect(result).toContain('clippy')
  })

  it('detects Python project from pyproject.toml', async () => {
    const fs = createTestFS({
      '/project/pyproject.toml': '[tool.pytest]\n[tool.ruff]',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Python')
    expect(result).toContain('pytest')
    expect(result).toContain('ruff')
  })

  it('detects Go project from go.mod', async () => {
    const fs = createTestFS({
      '/project/go.mod': 'module example.com/mymod',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('Go')
    expect(result).toContain('go test')
    expect(result).toContain('gofmt')
  })

  it('detects pnpm package manager', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        packageManager: 'pnpm@9.0.0',
      }),
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('pnpm')
  })

  it('handles complete TypeScript project', async () => {
    const fs = createTestFS({
      '/project/package.json': JSON.stringify({
        dependencies: { 'solid-js': '^1.0.0' },
        devDependencies: { vitest: '^1.0.0', typescript: '^5.0.0' },
      }),
      '/project/tsconfig.json': '{}',
      '/project/biome.json': '{}',
      '/project/vitest.config.ts': 'export default {}',
    })
    const result = await generateProjectRules('/project', fs)
    expect(result).toContain('TypeScript')
    expect(result).toContain('SolidJS')
    expect(result).toContain('Vitest')
    expect(result).toContain('Biome')
    expect(result).toContain('## Build')
    expect(result).toContain('## Testing')
    expect(result).toContain('## Code Quality')
    expect(result).toContain('## Code Style')
  })

  it('handles invalid package.json gracefully', async () => {
    const fs = createTestFS({
      '/project/package.json': 'not valid json',
    })
    const result = await generateProjectRules('/project', fs)
    // Should not throw, still produces output
    expect(result).toContain('# Project Instructions')
  })
})
