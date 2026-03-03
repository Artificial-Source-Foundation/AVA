/**
 * Zod schema for validating Recipe objects.
 */

import * as z from 'zod'

export const recipeParamSchema = z.object({
  name: z.string().min(1),
  description: z.string().optional(),
  default: z.string().optional(),
  required: z.boolean().optional(),
})

export const recipeStepSchema = z.object({
  name: z.string().min(1),
  tool: z.string().optional(),
  command: z.string().optional(),
  goal: z.string().optional(),
  recipe: z.string().optional(),
  args: z.record(z.string(), z.string()).optional(),
  parallel: z.boolean().optional(),
  condition: z.string().optional(),
  retry: z
    .object({
      maxAttempts: z.number().int().min(1),
      delayMs: z.number().int().min(0).optional(),
    })
    .optional(),
  onError: z.enum(['continue', 'abort']).optional(),
})

export const recipeSchema = z.object({
  name: z.string().min(1),
  description: z.string().optional(),
  version: z.string().optional(),
  params: z.array(recipeParamSchema).optional(),
  steps: z.array(recipeStepSchema).min(1),
  schedule: z.string().optional(),
})

export type ValidatedRecipe = z.infer<typeof recipeSchema>

/**
 * Validate a recipe object against the schema.
 * Throws a ZodError if validation fails.
 */
export function validateRecipe(data: unknown): ValidatedRecipe {
  return z.parse(recipeSchema, data)
}
