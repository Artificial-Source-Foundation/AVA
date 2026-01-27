/**
 * Delta9 Squadron System
 *
 * Wave-based batch agent execution.
 *
 * @example
 * ```typescript
 * import { getSquadronManager } from './squadrons/index.js'
 *
 * const manager = getSquadronManager(missionState, cwd, client)
 *
 * // Spawn squadron with 3 waves
 * const squadron = await manager.spawnSquadron({
 *   description: 'Implement Gallery page',
 *   waves: [
 *     { agents: [
 *       { type: 'scout', prompt: 'Scout codebase...' },
 *       { type: 'intel', prompt: 'Research patterns...' }
 *     ]},
 *     { agents: [
 *       { type: 'operator', prompt: 'Create GalleryGrid...' },
 *       { type: 'operator', prompt: 'Create GalleryImage...' }
 *     ]},
 *     { agents: [
 *       { type: 'validator', prompt: 'Verify implementation...' }
 *     ]}
 *   ]
 * })
 *
 * // Listen for events (for toast notifications)
 * manager.onEvent((event) => {
 *   if (event.type === 'wave_completed') {
 *     showToast(`Wave ${event.waveNumber} complete!`)
 *   }
 * })
 *
 * // Wait for completion
 * const result = await manager.waitForSquadron(squadron.id)
 * ```
 */

export * from './types.js'
export * from './manager.js'
