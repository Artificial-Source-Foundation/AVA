import { fileURLToPath } from 'node:url'
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
    include: ['src/**/*.{test,spec}.{ts,tsx}', 'packages/core/src/**/*.{test,spec}.ts'],
  },
  resolve: {
    conditions: ['development', 'browser'],
    alias: {
      '@ava/core': fileURLToPath(new URL('./packages/core/src/index.ts', import.meta.url)),
      '@ava/platform-tauri': fileURLToPath(
        new URL('./packages/platform-tauri/src/index.ts', import.meta.url)
      ),
      '@ava/platform-node': fileURLToPath(
        new URL('./packages/platform-node/src/index.ts', import.meta.url)
      ),
    },
  },
})
