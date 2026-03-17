# OSV Vulnerability Scanning

> Status: Idea (not implemented)
> Source: Original (supply chain safety)
> Effort: Medium

## Summary
Check packages against the OSV (Open Source Vulnerabilities) database before allowing install commands. Parses package manager commands to extract ecosystem and package name, then queries the OSV API for known vulnerabilities. Currently a stub with no actual HTTP calls.

## Key Design Points
- `ScanResult`: Safe, Vulnerable(advisory IDs), or Unknown
- OSV API at `https://api.osv.dev/v1/query` with ecosystem+package JSON body
- `extract_package_from_command` supports: npx, npm install, pip/pip3 install, gem install, cargo install
- Scoped package names (npm `@scope/package`) handled by stripping leading `@`
- Five ecosystems: npm, PyPI, RubyGems, crates.io, Go (pkg.go.dev)

## Integration Notes
- Would integrate with the sandbox middleware to intercept install commands
- Needs reqwest HTTP client wired in for actual API calls
- Could block or warn on packages with known CVEs before installation
