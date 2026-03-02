import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { AzureOpenAIClient } from './client.js'

export function activate(api: ExtensionAPI): Disposable {
  return api.registerProvider('azure', () => {
    const endpoint = process.env.AZURE_OPENAI_ENDPOINT ?? ''
    const apiKey = process.env.AZURE_OPENAI_API_KEY ?? ''
    const deploymentId = process.env.AZURE_OPENAI_DEPLOYMENT_ID ?? ''
    return new AzureOpenAIClient({ endpoint, apiKey, deploymentId })
  })
}
