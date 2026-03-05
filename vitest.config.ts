import { fileURLToPath } from 'node:url'
import solidPlugin from 'vite-plugin-solid'
import { defineConfig } from 'vitest/config'

const stub = (name: string) => fileURLToPath(new URL(`./src/stubs/${name}.ts`, import.meta.url))

export default defineConfig({
  plugins: [solidPlugin()],
  test: {
    environment: 'jsdom',
    globals: true,
    // SolidJS testing setup
    deps: {
      optimizer: {
        web: {
          include: ['solid-js', 'lucide-solid'],
        },
      },
    },
    server: {
      deps: {
        inline: ['lucide-solid'],
      },
    },
    // Coverage configuration
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html'],
      exclude: ['node_modules/**', 'src-tauri/**', '**/*.config.{js,ts}', '**/*.d.ts'],
    },
    // Test file patterns
    include: [
      'src/**/*.{test,spec}.{ts,tsx}',
      'packages/core/src/**/*.{test,spec}.ts',
      'packages/core-v2/src/**/*.{test,spec}.ts',
      'packages/platform-*/src/**/*.{test,spec}.ts',
      'packages/extensions/**/*.{test,spec}.ts',
      'cli/src/**/*.{test,spec}.ts',
      'docs/examples/**/*.{test,spec}.ts',
      'tests/e2e/**/*.{test,spec}.ts',
    ],
  },
  resolve: {
    conditions: ['development', 'browser'],
    alias: {
      // core-v2 submodule aliases (must come before main @ava/core-v2)
      '@ava/core-v2/agent': fileURLToPath(
        new URL('./packages/core-v2/src/agent/index.ts', import.meta.url)
      ),
      '@ava/core-v2/llm': fileURLToPath(
        new URL('./packages/core-v2/src/llm/index.ts', import.meta.url)
      ),
      '@ava/core-v2/tools': fileURLToPath(
        new URL('./packages/core-v2/src/tools/index.ts', import.meta.url)
      ),
      '@ava/core-v2/extensions': fileURLToPath(
        new URL('./packages/core-v2/src/extensions/index.ts', import.meta.url)
      ),
      '@ava/core-v2/platform': fileURLToPath(
        new URL('./packages/core-v2/src/platform.ts', import.meta.url)
      ),
      '@ava/core-v2/bus': fileURLToPath(
        new URL('./packages/core-v2/src/bus/index.ts', import.meta.url)
      ),
      '@ava/core-v2/config': fileURLToPath(
        new URL('./packages/core-v2/src/config/index.ts', import.meta.url)
      ),
      '@ava/core-v2/session': fileURLToPath(
        new URL('./packages/core-v2/src/session/index.ts', import.meta.url)
      ),
      '@ava/core-v2/logger': fileURLToPath(
        new URL('./packages/core-v2/src/logger/index.ts', import.meta.url)
      ),
      '@ava/core-v2/__test-utils__/mock-platform': fileURLToPath(
        new URL('./packages/core-v2/src/__test-utils__/mock-platform.ts', import.meta.url)
      ),
      '@ava/core-v2/__test-utils__/mock-extension-api': fileURLToPath(
        new URL('./packages/core-v2/src/__test-utils__/mock-extension-api.ts', import.meta.url)
      ),
      '@ava/core-v2': fileURLToPath(new URL('./packages/core-v2/src/index.ts', import.meta.url)),
      '@ava/core': fileURLToPath(new URL('./packages/core/src/index.ts', import.meta.url)),
      '@ava/platform-tauri': fileURLToPath(
        new URL('./packages/platform-tauri/src/index.ts', import.meta.url)
      ),
      '@ava/platform-node/v2': fileURLToPath(
        new URL('./packages/platform-node/src/v2.ts', import.meta.url)
      ),
      '@ava/platform-node': fileURLToPath(
        new URL('./packages/platform-node/src/index.ts', import.meta.url)
      ),
      // Tauri API stubs — each module gets its own file so vi.mock works correctly
      '@tauri-apps/api/core': stub('tauri-api-core'),
      '@tauri-apps/api/event': stub('tauri-api-event'),
      '@tauri-apps/plugin-opener': stub('tauri-plugin-opener'),
      '@tauri-apps/plugin-dialog': stub('tauri-plugin-dialog'),
      '@tauri-apps/plugin-fs': stub('tauri-plugin-fs'),
      '@tauri-apps/plugin-shell': stub('tauri-plugin-shell'),
      '@tauri-apps/plugin-sql': stub('tauri-plugin-sql'),
      '@tauri-apps/plugin-window-state': stub('tauri-plugin-window-state'),
    },
  },
})
