import { parseCliOptions, runRustHotpathBenchmark } from './rust-hotpath-benchmark-lib.js'

async function main(): Promise<void> {
  const options = parseCliOptions(process.argv.slice(2))
  await runRustHotpathBenchmark(options)
}

main().catch((error: unknown) => {
  const message = error instanceof Error ? (error.stack ?? error.message) : String(error)
  console.error(message)
  process.exit(1)
})
