import { defineConfig, type Plugin } from 'vite'
import { analyzer } from 'vite-bundle-analyzer'
import solid from 'vite-plugin-solid'

// @ts-expect-error process is a nodejs global
const host = process.env.TAURI_DEV_HOST
// @ts-expect-error process is a nodejs global
const analyze = process.env.ANALYZE === 'true'

// Plugin to exclude reference-code from being scanned
function excludeReferenceCode(): Plugin {
  return {
    name: 'exclude-reference-code',
    resolveId(id) {
      if (id.includes('docs/reference-code')) {
        return { id, external: true }
      }
      return null
    },
    load(id) {
      if (id.includes('docs/reference-code')) {
        return ''
      }
      return null
    },
  }
}

// https://vite.dev/config/
export default defineConfig(async () => ({
  plugins: [
    excludeReferenceCode(),
    solid(),
    // Bundle analyzer - only in analyze mode
    analyze &&
      analyzer({
        analyzerMode: 'static',
        fileName: 'bundle-report',
        openAnalyzer: true,
      }),
  ].filter(Boolean),

  // Define globals for browser compatibility (Node.js polyfills)
  define: {
    global: 'globalThis',
    'process.env': JSON.stringify({}),
    'process.platform': JSON.stringify('browser'),
    'process.cwd': '(() => "/")',
  },

  // Exclude reference code and other non-source directories from optimization
  optimizeDeps: {
    exclude: ['docs', 'cli', 'packages'],
    entries: ['src/**/*.{ts,tsx}'],
  },

  // Build configuration - exclude reference code
  build: {
    rollupOptions: {
      external: [/docs\/reference-code/],
    },
  },

  // Vite options tailored for Tauri development and only applied in `tauri dev` or `tauri build`
  //
  // 1. prevent Vite from obscuring rust errors
  clearScreen: false,
  // 2. tauri expects a fixed port, fail if that port is not available
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host
      ? {
          protocol: 'ws',
          host,
          port: 1421,
        }
      : undefined,
    watch: {
      // 3. tell Vite to ignore watching `src-tauri` and reference code
      ignored: ['**/src-tauri/**', '**/docs/reference-code/**'],
    },
    fs: {
      // Deny access to reference code directory
      deny: ['**/docs/reference-code/**'],
    },
  },
}))
