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
      'cli/src/**/*.{test,spec}.ts',
    ],
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
