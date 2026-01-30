import solidPlugin from 'vite-plugin-solid'
import { defineConfig } from 'vitest/config'

export default defineConfig({
  plugins: [solidPlugin()],
  test: {
    environment: 'jsdom',
    globals: true,
    // SolidJS testing setup
    deps: {
      optimizer: {
        web: {
          include: ['solid-js'],
        },
      },
    },
    // Coverage configuration
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html'],
      exclude: ['node_modules/**', 'src-tauri/**', '**/*.config.{js,ts}', '**/*.d.ts'],
    },
    // Test file patterns
    include: ['src/**/*.{test,spec}.{ts,tsx}'],
  },
  resolve: {
    conditions: ['development', 'browser'],
  },
})
