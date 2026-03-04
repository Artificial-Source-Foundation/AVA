use ava_agent::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};
use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion};

struct StaticReflectionAgent;

impl ReflectionAgent for StaticReflectionAgent {
    fn generate_fix(&self, _error_kind: ErrorKind, _result: &ToolResult) -> Result<String, String> {
        Ok("node scripts/fix.js".to_string())
    }
}

struct StaticToolExecutor {
    result: ToolResult,
}

impl ToolExecutor for StaticToolExecutor {
    fn execute_tool(&self, _input: &str) -> ToolResult {
        self.result.clone()
    }
}

fn reflection_error_classification(c: &mut Criterion) {
    let mut group = c.benchmark_group("reflection_error_classification");

    let syntax_error = "SyntaxError: unexpected token ')'";
    let import_error = "Cannot find module 'chalk' imported from ./src/main.ts";
    let type_error = "TypeError: value is not assignable to target type";
    let command_error = "bash: npm: command not found";

    group.bench_with_input(
        BenchmarkId::new("kind", "syntax"),
        &syntax_error,
        |b, error| b.iter(|| ReflectionLoop::analyze_error(black_box(error))),
    );
    group.bench_with_input(
        BenchmarkId::new("kind", "import"),
        &import_error,
        |b, error| b.iter(|| ReflectionLoop::analyze_error(black_box(error))),
    );
    group.bench_with_input(BenchmarkId::new("kind", "type"), &type_error, |b, error| {
        b.iter(|| ReflectionLoop::analyze_error(black_box(error)))
    });
    group.bench_with_input(
        BenchmarkId::new("kind", "command"),
        &command_error,
        |b, error| b.iter(|| ReflectionLoop::analyze_error(black_box(error))),
    );

    group.finish();
}

fn single_fix_attempt_path(c: &mut Criterion) {
    let agent = StaticReflectionAgent;
    let executor = StaticToolExecutor {
        result: ToolResult {
            output: "fixed".to_string(),
            error: None,
        },
    };
    let loop_controller = ReflectionLoop::new(&agent, &executor);

    c.bench_function("reflection_single_fix_attempt", |b| {
        b.iter(|| {
            loop_controller.reflect_and_fix(ToolResult {
                output: "failed".to_string(),
                error: Some(black_box("SyntaxError: unexpected token".to_string())),
            })
        })
    });
}

criterion_group!(
    reflection_hotpaths,
    reflection_error_classification,
    single_fix_attempt_path
);
criterion_main!(reflection_hotpaths);
