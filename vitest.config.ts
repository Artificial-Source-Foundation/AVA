import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    // Exclude reference code - external plugin examples with missing deps
    exclude: ['**/node_modules/**', '**/dist/**', '**/docs/reference-code/**'],
    // Include only src tests
    include: ['src/**/*.test.ts', 'tests/**/*.test.ts'],
    // Coverage configuration
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html', 'lcov'],
      exclude: [
        'node_modules/**',
        'dist/**',
        'docs/**',
        '**/*.test.ts',
        '**/*.d.ts',
        'src/types/**',
      ],
      thresholds: {
        statements: 70,
        branches: 70,
        functions: 70,
        lines: 70,
      },
    },
    // Globals for cleaner test files
    globals: true,
    // Environment
    environment: 'node',
  },
})
