import { fileURLToPath } from 'node:url'
import { defineConfig, type Plugin } from 'vite'
import { analyzer } from 'vite-bundle-analyzer'
import solid from 'vite-plugin-solid'

const host = process.env.TAURI_DEV_HOST
const analyze = process.env.ANALYZE === 'true'

const STUB_PATH = fileURLToPath(new URL('./src/stubs/node-stub.ts', import.meta.url))

// Node.js built-in modules that some transitive dependencies may import.
// In the browser (Tauri webview), these get replaced with no-op stubs.
const NODE_BUILTINS = new Set([
  'node:child_process',
  'child_process',
  'node:fs',
  'node:fs/promises',
  'fs',
  'fs/promises',
  'node:path',
  'path',
  'node:os',
  'os',
  'node:crypto',
  'crypto',
  'node:url',
  'url',
  'node:buffer',
  'buffer',
  'node:stream',
  'stream',
  'node:process',
  'process',
  'node:events',
  'events',
  'node:util',
  'util',
  'node:net',
  'net',
  'node:http',
  'http',
  'node:https',
  'https',
  'cross-spawn',
])

/**
 * Plugin that redirects all Node.js built-in imports to a stub module.
 * Uses `resolveId` hook which fires before `vite:import-analysis`,
 * ensuring any Node.js built-in imports are handled.
 */
function stubNodeBuiltins(): Plugin {
  return {
    name: 'stub-node-builtins',
    enforce: 'pre',
    resolveId(id) {
      if (NODE_BUILTINS.has(id)) {
        return STUB_PATH
      }
      return null
    },
  }
}

/**
 * Inject a global `process` polyfill before any app code runs.
 * The `define` config handles static `process.env`/`process.platform` replacements,
 * but dynamic access like `process.cwd()` needs a real global object.
 */
function injectProcessPolyfill(): Plugin {
  return {
    name: 'inject-process-polyfill',
    transformIndexHtml(html) {
      return html.replace(
        '<head>',
        `<head><script>globalThis.process=globalThis.process||{};globalThis.process.env=globalThis.process.env||{};globalThis.process.platform="browser";globalThis.process.cwd=function(){return "/"};globalThis.process.argv=[];globalThis.process.version="v0.0.0";globalThis.process.versions={};globalThis.process.on=function(){return globalThis.process};globalThis.process.off=function(){return globalThis.process};globalThis.process.nextTick=function(fn){Promise.resolve().then(fn)};</script>`
      )
    },
  }
}

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
    injectProcessPolyfill(),
    stubNodeBuiltins(),
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

  resolve: {},

  // Define globals for browser compatibility (Node.js polyfills)
  define: {
    global: 'globalThis',
    'process.env': JSON.stringify({}),
    'process.platform': JSON.stringify('browser'),
  },

  // Exclude reference code, Node.js-only modules, and other non-source directories from optimization
  optimizeDeps: {
    exclude: ['docs', 'cli', 'puppeteer', 'puppeteer-core'],
    include: ['@codemirror/state', '@codemirror/view'],
    entries: ['src/**/*.{ts,tsx}'],
  },

  // Build configuration - code splitting + external exclusions
  build: {
    rollupOptions: {
      external: [/docs\/reference-code/, 'puppeteer', 'puppeteer-core'],
      output: {
        manualChunks(id) {
          // CodeMirror is heavy — isolate it
          if (id.includes('@codemirror') || id.includes('@lezer')) {
            return 'codemirror'
          }
          // Lucide icons
          if (id.includes('lucide-solid')) {
            return 'icons'
          }
          // SolidJS runtime
          if (id.includes('solid-js') || id.includes('solid-primitives')) {
            return 'solid'
          }
          // All other node_modules
          if (id.includes('node_modules')) {
            return 'vendor'
          }
        },
      },
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
    proxy: {
      '/__chatgpt_proxy': {
        target: 'https://chatgpt.com',
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/__chatgpt_proxy/, ''),
      },
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://localhost:8080',
        ws: true,
      },
    },
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
