import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    // Exclude reference code - external plugin examples with missing deps
    exclude: [
      '**/node_modules/**',
      '**/dist/**',
      '**/docs/reference-code/**',
    ],
    // Include only src tests
    include: ['src/**/*.test.ts', 'tests/**/*.test.ts'],
  },
})
