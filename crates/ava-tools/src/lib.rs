//! AVA Tools — tool system for file operations, shell commands, and more.
//!
//! This crate implements the tool trait and registry, including:
//! - File read/write/edit operations
//! - Shell and bash command execution
//! - Git operations and search tools

pub mod browser;
pub mod core;
pub mod edit;
pub mod git;
pub mod lint_middleware;
pub mod mcp_bridge;
pub mod monitor;
pub mod permission_middleware;
pub mod registry;
