#!/usr/bin/env node
/**
 * Find a free port starting from 1420 and export it as DEV_PORT.
 * Used by `pnpm run dev` so Vite and Tauri share the same port
 * without conflicting with other apps.
 */
import getPort from 'get-port'

const port = await getPort({ port: [1420, 1421, 1422, 1423, 1424, 1425] })
process.stdout.write(String(port))
