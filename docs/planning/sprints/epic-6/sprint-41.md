# Sprint 41: Release

**Epic:** Ship It (Epic 6)  
**Duration:** 2 weeks  
**Goal:** Production release

## Stories

### Story 6.5: Release Preparation
**Points:** 15 (Team: 3/4 sprint)

**What to do:**

**1. Version Bump**
```bash
# Update version in all Cargo.toml
cargo workspaces version minor --yes

# Update CHANGELOG
cat > CHANGELOG.md << 'EOF'
# Changelog

## 2.0.0 (2026-03-15)

### Major Changes
- **Rust Backend**: Complete rewrite in Rust for 50x better performance
- **Streaming Edits**: Apply changes as LLM generates them (0.5s latency)
- **OS Sandboxing**: Landlock/Seatbelt for 100ms sandbox startup
- **9 Edit Strategies**: 90% edit success rate with auto-recovery
- **PageRank Repo Map**: Better context selection
- **9 Context Condensers**: 40% more efficient context management
- **MCP Server Mode**: Other agents can use AVA tools

### Performance
- Startup: 3s → 100ms (30x faster)
- Edit latency: 3s → 0.5s (6x faster)
- Memory: 300MB → 50MB (6x less)
- Binary: ~10MB (vs hundreds with Node)

### Security
- OS-level sandboxing (no Docker needed)
- Tree-sitter bash security analysis
- Dynamic permission escalation

### New Features
- 35 high-quality tools (reduced from 55)
- Per-hunk review UI
- Real-time LSP diagnostics
- Multi-strategy context compaction

### Breaking Changes
- Tool API now uses Tauri commands
- Configuration format updated
- See migration guide
EOF
```

**2. Build Distribution**
```bash
# Build for all platforms
cargo build --release --target x86_64-unknown-linux-gnu
cargo build --release --target x86_64-apple-darwin
cargo build --release --target aarch64-apple-darwin
cargo build --release --target x86_64-pc-windows-msvc

# Create installers
# - macOS: .dmg
# - Linux: .AppImage, .deb, .rpm
# - Windows: .msi
```

**3. Documentation**
- [ ] API docs generated (`cargo doc`)
- [ ] User guide updated
- [ ] Migration guide complete
- [ ] Release notes written

**Acceptance Criteria:**
- [ ] Version bumped
- [ ] Changelog complete
- [ ] Builds for all platforms
- [ ] Docs ready

---

### Story 6.6: Launch
**Points:** 15 (Team: 1/4 sprint)

**What to do:**

**1. Release Checklist**
- [ ] All tests passing
- [ ] No P0/P1 bugs
- [ ] Performance targets met
- [ ] Documentation complete
- [ ] Builds tested on all platforms
- [ ] GitHub release created
- [ ] Binaries uploaded
- [ ] Homebrew formula updated
- [ ] AUR package updated
- [ ] Winget manifest updated

**2. Announcement**
```markdown
# AVA 2.0 is here! 🚀

After 9 months of work, we're excited to release AVA 2.0 with a complete Rust backend.

**What's new:**
- 50x faster performance
- Real-time streaming edits
- OS-level sandboxing
- 90% edit success rate

**Get it:**
- macOS: `brew install ava`
- Linux: Download from releases
- Windows: `winget install ava`

Read more: [link to blog post]
```

**3. Post-Release**
- Monitor crash reports
- Respond to issues
- Plan 2.1

**Acceptance Criteria:**
- [ ] Released on all channels
- [ ] Announcement published
- [ ] Monitoring in place

---

## Epic 6 Complete!

## 🎉 AVA 2.0 SHIPPED! 🎉

**Success Criteria:**
- [x] 100% Rust backend
- [x] 35 high-quality tools
- [x] Best-in-class performance
- [x] Production ready
- [x] Documentation complete

**Total Timeline:** 9 months (Sprints 24-41)
**Total Stories:** 51
**Total Points:** 642

**Result:** The best AI coding agent on the market.

---

## Next Steps (Post-2.0)

- **2.1**: Bug fixes, small features
- **2.2**: Extension marketplace
- **3.0**: Advanced features (branch viz, team templates)

**Thank you for building AVA 2.0!**
