import { describe, expect, it } from 'vitest'
import { generateIndexTs, generateManifest, TEMPLATES } from './plugin-templates'

describe('plugin-templates', () => {
  it('generates plugin.toml-style manifests', () => {
    const manifest = generateManifest(TEMPLATES[0], 'Hello Plugin', 'Example plugin', 'ASF')
    expect(manifest).toContain('[plugin]')
    expect(manifest).toContain('[runtime]')
    expect(manifest).toContain('[hooks]')
    expect(manifest).toContain('name = "hello-plugin"')
  })

  it('provider template uses the real SDK and runtime template string', () => {
    const providerTemplate = TEMPLATES.find((template) => template.id === 'provider')
    const code = generateIndexTs(providerTemplate, 'Demo Provider', 'Auth example')
    expect(code).toContain("import { createPlugin } from '@ava-ai/plugin'")
    expect(code).toContain("Authorization: `Bearer ${process.env.DEMO_PROVIDER_TOKEN ?? ''}`")
  })
})
