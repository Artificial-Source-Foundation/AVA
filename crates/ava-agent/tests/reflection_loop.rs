#![allow(unsafe_code)] // CountingAllocator benchmark needs unsafe GlobalAlloc

use std::cell::{Cell, RefCell};

use ava_agent::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};

struct CountingAllocator;

thread_local! {
    static THREAD_ALLOCATION_COUNT: Cell<usize> = const { Cell::new(0) };
}

#[global_allocator]
static GLOBAL_ALLOCATOR: CountingAllocator = CountingAllocator;

// SAFETY: Delegates to the system allocator and only increments a counter.
unsafe impl std::alloc::GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: std::alloc::Layout) -> *mut u8 {
        THREAD_ALLOCATION_COUNT.with(|count| count.set(count.get() + 1));
        unsafe { std::alloc::System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: std::alloc::Layout) {
        unsafe { std::alloc::System.dealloc(ptr, layout) }
    }
}

fn reset_thread_allocation_count() {
    THREAD_ALLOCATION_COUNT.with(|count| count.set(0));
}

fn thread_allocation_count() -> usize {
    THREAD_ALLOCATION_COUNT.with(Cell::get)
}

struct StubReflectionAgent {
    calls: Cell<usize>,
    fix_to_return: RefCell<Result<String, String>>,
}

impl StubReflectionAgent {
    fn with_fix(fix: Result<String, String>) -> Self {
        Self {
            calls: Cell::new(0),
            fix_to_return: RefCell::new(fix),
        }
    }
}

impl ReflectionAgent for StubReflectionAgent {
    fn generate_fix(&self, _error_kind: ErrorKind, _result: &ToolResult) -> Result<String, String> {
        self.calls.set(self.calls.get() + 1);
        self.fix_to_return.borrow().clone()
    }
}

struct StubToolExecutor {
    calls: Cell<usize>,
    result_to_return: RefCell<ToolResult>,
}

impl StubToolExecutor {
    fn with_result(result: ToolResult) -> Self {
        Self {
            calls: Cell::new(0),
            result_to_return: RefCell::new(result),
        }
    }
}

impl ToolExecutor for StubToolExecutor {
    fn execute_tool(&self, _input: &str) -> ToolResult {
        self.calls.set(self.calls.get() + 1);
        self.result_to_return.borrow().clone()
    }
}

#[test]
fn error_classifier_detects_syntax_import_type_and_command_errors() {
    assert_eq!(
        ReflectionLoop::analyze_error("SyntaxError: unexpected token"),
        Some(ErrorKind::Syntax)
    );
    assert_eq!(
        ReflectionLoop::analyze_error("Cannot find module 'chalk'"),
        Some(ErrorKind::Import)
    );
    assert_eq!(
        ReflectionLoop::analyze_error("TypeError: undefined is not a function"),
        Some(ErrorKind::Type)
    );
    assert_eq!(
        ReflectionLoop::analyze_error("bash: npm: command not found"),
        Some(ErrorKind::Command)
    );
}

#[test]
fn error_classifier_is_case_insensitive_without_allocating() {
    let test_cases = [
        ("SyNtAxErRoR: unexpected token", ErrorKind::Syntax),
        ("Cannot Find Module 'chalk'", ErrorKind::Import),
        ("TyPeErRoR: undefined is not a function", ErrorKind::Type),
        ("BASH: NPM: COMMAND NOT FOUND", ErrorKind::Command),
    ];

    for (error, expected) in test_cases {
        reset_thread_allocation_count();
        let actual = ReflectionLoop::analyze_error(error);
        let allocations_after = thread_allocation_count();

        assert_eq!(actual, Some(expected));
        assert_eq!(
            allocations_after, 0,
            "analyze_error allocated for input: {error}"
        );
    }
}

#[test]
fn error_classifier_handles_long_messages_with_category_tokens_without_allocating() {
    let long_noise = "A".repeat(16 * 1024);
    let test_cases = [
        (format!("{long_noise} ... UnExPeCtEd ToKeN ... {long_noise}"), ErrorKind::Syntax),
        (format!("{long_noise} ... NO MODULE NAMED requests ... {long_noise}"), ErrorKind::Import),
        (format!("{long_noise} ... MiSmAtChEd TyPeS ... {long_noise}"), ErrorKind::Type),
        (
            format!(
                "{long_noise} ... IS NOT RECOGNIZED AS AN INTERNAL OR EXTERNAL COMMAND ... {long_noise}"
            ),
            ErrorKind::Command,
        ),
    ];

    for (error, expected) in test_cases {
        reset_thread_allocation_count();
        let actual = ReflectionLoop::analyze_error(&error);
        let allocations_after = thread_allocation_count();

        assert_eq!(actual, Some(expected));
        assert_eq!(
            allocations_after, 0,
            "analyze_error allocated for long input classified as {expected:?}"
        );
    }
}

#[test]
fn reflection_triggers_one_fix_attempt_when_result_has_error() {
    let agent = StubReflectionAgent::with_fix(Ok("apply fix".to_string()));
    let executor = StubToolExecutor::with_result(ToolResult {
        output: "fixed".to_string(),
        error: None,
    });
    let loop_controller = ReflectionLoop::new(&agent, &executor);

    let final_result = loop_controller.reflect_and_fix(ToolResult {
        output: "failed".to_string(),
        error: Some("SyntaxError: unexpected token".to_string()),
    });

    assert_eq!(final_result.output, "fixed");
    assert!(final_result.error.is_none());
    assert_eq!(agent.calls.get(), 1);
    assert_eq!(executor.calls.get(), 1);
}

#[test]
fn reflection_skips_when_result_has_no_error() {
    let agent = StubReflectionAgent::with_fix(Ok("unused fix".to_string()));
    let executor = StubToolExecutor::with_result(ToolResult {
        output: "unused".to_string(),
        error: None,
    });
    let loop_controller = ReflectionLoop::new(&agent, &executor);

    let original = ToolResult {
        output: "already good".to_string(),
        error: None,
    };
    let final_result = loop_controller.reflect_and_fix(original.clone());

    assert_eq!(final_result, original);
    assert_eq!(agent.calls.get(), 0);
    assert_eq!(executor.calls.get(), 0);
}

#[test]
fn reflection_returns_original_result_when_fix_generation_fails() {
    let agent = StubReflectionAgent::with_fix(Err("generation failed".to_string()));
    let executor = StubToolExecutor::with_result(ToolResult {
        output: "should not run".to_string(),
        error: None,
    });
    let loop_controller = ReflectionLoop::new(&agent, &executor);

    let original = ToolResult {
        output: "failed".to_string(),
        error: Some("TypeError: invalid assignment".to_string()),
    };
    let final_result = loop_controller.reflect_and_fix(original.clone());

    assert_eq!(final_result, original);
    assert_eq!(agent.calls.get(), 1);
    assert_eq!(executor.calls.get(), 0);
}

#[test]
fn reflection_does_not_retry_more_than_once_per_call() {
    let agent = StubReflectionAgent::with_fix(Ok("single fix".to_string()));
    let executor = StubToolExecutor::with_result(ToolResult {
        output: "still failing".to_string(),
        error: Some("TypeError: still broken".to_string()),
    });
    let loop_controller = ReflectionLoop::new(&agent, &executor);

    let final_result = loop_controller.reflect_and_fix(ToolResult {
        output: "failed".to_string(),
        error: Some("TypeError: wrong type".to_string()),
    });

    assert_eq!(final_result.output, "still failing");
    assert!(final_result.error.is_some());
    assert_eq!(agent.calls.get(), 1);
    assert_eq!(executor.calls.get(), 1);
}

#[test]
fn error_classifier_table_driven_consistency_including_case_variants() {
    let test_cases = [
        ("syntax error near token", ErrorKind::Syntax),
        ("importerror: missing dependency", ErrorKind::Import),
        ("type error in assignment", ErrorKind::Type),
        ("command not found: npm", ErrorKind::Command),
    ];

    for (pattern, expected) in test_cases {
        for candidate in [
            pattern.to_string(),
            pattern.to_ascii_uppercase(),
            pattern
                .chars()
                .enumerate()
                .map(|(index, ch)| {
                    if index % 2 == 0 {
                        ch.to_ascii_uppercase()
                    } else {
                        ch.to_ascii_lowercase()
                    }
                })
                .collect(),
        ] {
            assert_eq!(ReflectionLoop::analyze_error(&candidate), Some(expected));
        }
    }
}
