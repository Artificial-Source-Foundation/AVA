/**
 * Cost Utilities
 * Pure functions for cost formatting.
 *
 * Cost estimation is handled by the Rust backend (ava-llm) which has access
 * to the compiled-in model registry with up-to-date pricing. The frontend
 * only needs to format pre-computed cost values for display.
 */

/** Format cost as currency string */
export function formatCost(cost: number): string {
  if (cost < 0.01) {
    return `$${(cost * 1000).toFixed(2)}m`
  }
  return `$${cost.toFixed(4)}`
}
