import path from 'node:path'
import { defineConfig } from 'vitest/config'

export default defineConfig({
  resolve: {
    alias: {
      '@ava/core-v2/llm': path.resolve(__dirname, '../core-v2/src/llm/index.ts'),
      '@ava/core-v2/tools': path.resolve(__dirname, '../core-v2/src/tools/index.ts'),
      '@ava/core-v2/extensions': path.resolve(__dirname, '../core-v2/src/extensions/index.ts'),
      '@ava/core-v2/platform': path.resolve(__dirname, '../core-v2/src/platform.ts'),
      '@ava/core-v2/bus': path.resolve(__dirname, '../core-v2/src/bus/index.ts'),
      '@ava/core-v2/config': path.resolve(__dirname, '../core-v2/src/config/index.ts'),
      '@ava/core-v2/session': path.resolve(__dirname, '../core-v2/src/session/index.ts'),
      '@ava/core-v2/logger': path.resolve(__dirname, '../core-v2/src/logger/index.ts'),
      '@ava/core-v2': path.resolve(__dirname, '../core-v2/src/index.ts'),
    },
  },
  test: {
    include: ['**/*.test.ts'],
    globals: false,
    restoreMocks: true,
  },
})
