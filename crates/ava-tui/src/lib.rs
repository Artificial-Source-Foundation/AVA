//! AVA TUI — terminal user interface for the AVA CLI.
//!
//! This crate provides the Ratatui-based TUI including:
//! - Interactive chat interface with markdown rendering
//! - Headless mode for scripting and automation
//! - Session management and message history

pub mod app;
#[cfg(feature = "voice")]
pub mod audio;
pub mod auth;
#[cfg(feature = "benchmark")]
pub mod benchmark;
#[cfg(feature = "benchmark")]
pub mod benchmark_harness;
#[cfg(feature = "benchmark")]
pub mod benchmark_import;
#[cfg(feature = "benchmark")]
pub mod benchmark_reporting;
#[cfg(feature = "benchmark")]
pub(crate) mod benchmark_support;
#[cfg(feature = "benchmark")]
pub mod benchmark_tasks;
pub mod config;
pub mod event;
pub mod headless;
pub mod hooks;
pub mod plugin_commands;
pub mod rendering;
pub mod review;
pub mod session_summary;
pub mod state;
pub mod text_utils;
#[cfg(feature = "voice")]
pub mod transcribe;
pub mod ui;
pub mod updater;
#[cfg(feature = "web")]
pub mod web;
pub mod widgets;
