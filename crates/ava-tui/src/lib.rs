//! AVA TUI — terminal user interface for the AVA CLI.
//!
//! This crate provides the Ratatui-based TUI including:
//! - Interactive chat interface with markdown rendering
//! - Headless mode for scripting and automation
//! - Session management and message history

#![allow(dead_code)]

pub mod app;
#[cfg(feature = "voice")]
pub mod audio;
pub mod config;
pub mod event;
pub mod headless;
pub mod rendering;
pub mod review;
pub mod state;
#[cfg(feature = "voice")]
pub mod transcribe;
pub mod ui;
pub mod widgets;
