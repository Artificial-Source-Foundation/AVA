import oxlint from 'eslint-plugin-oxlint'
import solid from 'eslint-plugin-solid/configs/typescript'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  // TypeScript base config
  ...tseslint.configs.recommended,
  // SolidJS-specific rules (TypeScript variant)
  {
    ...solid,
    files: ['src/**/*.{ts,tsx}'],
  },
  // Disable rules already covered by Oxlint (spread array)
  ...oxlint.configs['flat/recommended'],
  // Global ignores
  {
    ignores: ['node_modules/**', 'dist/**', 'src-tauri/**', '*.config.js', '*.config.ts'],
  }
)
