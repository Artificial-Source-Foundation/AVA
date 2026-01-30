import { defineConfig } from 'vite'
import { analyzer } from 'vite-bundle-analyzer'
import solid from 'vite-plugin-solid'

// @ts-expect-error process is a nodejs global
const host = process.env.TAURI_DEV_HOST
// @ts-expect-error process is a nodejs global
const analyze = process.env.ANALYZE === 'true'

// https://vite.dev/config/
export default defineConfig(async () => ({
  plugins: [
    solid(),
    // Bundle analyzer - only in analyze mode
    analyze &&
      analyzer({
        analyzerMode: 'static',
        fileName: 'bundle-report',
        openAnalyzer: true,
      }),
  ].filter(Boolean),

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
      // 3. tell Vite to ignore watching `src-tauri`
      ignored: ['**/src-tauri/**'],
    },
  },
}))
