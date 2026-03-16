/**
 * Sandbox Middleware — Stub
 *
 * Sandbox enforcement is now handled by the Rust backend.
 * This module is retained as a stub for backward compatibility.
 */

export function createSandboxMiddleware() {
  return { priority: 10, before: async (_ctx: unknown) => _ctx }
}
