use ava_validator::{
    validate_with_retry, CompilationValidator, FixGenerator, SyntaxValidator, ValidationPipeline,
    ValidationResult, Validator, DEFAULT_MAX_ATTEMPTS,
};
use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion};

struct RetryFixer;

impl FixGenerator for RetryFixer {
    fn generate_fix(
        &self,
        content: &str,
        _failure: &ValidationResult,
        attempt: usize,
    ) -> Option<String> {
        if attempt != 1 {
            return None;
        }

        if content.contains("compile_error!\"fail\"") {
            Some(content.replace("compile_error!\"fail\"", "1 + 1"))
        } else {
            None
        }
    }
}

fn build_balanced_source(repetitions: usize) -> String {
    let mut source = String::from("fn run() {\n");
    for index in 0..repetitions {
        source.push_str("    let value_");
        source.push_str(&index.to_string());
        source.push_str(" = (");
        source.push_str(&(index + 1).to_string());
        source.push_str(" + {");
        source.push_str(&(index + 2).to_string());
        source.push_str("});\n");
    }
    source.push_str("}\n");
    source
}

fn syntax_validation_hotpaths(c: &mut Criterion) {
    let validator = SyntaxValidator;
    let mut group = c.benchmark_group("syntax_validation");

    let small = build_balanced_source(8);
    let medium = build_balanced_source(80);
    let large = build_balanced_source(800);

    group.bench_with_input(BenchmarkId::new("payload", "small"), &small, |b, source| {
        b.iter(|| validator.validate(black_box(source.as_str())));
    });
    group.bench_with_input(
        BenchmarkId::new("payload", "medium"),
        &medium,
        |b, source| b.iter(|| validator.validate(black_box(source.as_str()))),
    );
    group.bench_with_input(BenchmarkId::new("payload", "large"), &large, |b, source| {
        b.iter(|| validator.validate(black_box(source.as_str())));
    });

    group.finish();
}

fn retry_pipeline_hotpath(c: &mut Criterion) {
    let pipeline = ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator);
    let fixer = RetryFixer;
    let source = "fn main() { compile_error!\"fail\"; }\n";

    c.bench_function("retry_pipeline_fail_fix_pass", |b| {
        b.iter(|| {
            let _ = validate_with_retry(
                black_box(&pipeline),
                black_box(source),
                black_box(&fixer),
                black_box(DEFAULT_MAX_ATTEMPTS),
            );
        });
    });
}

criterion_group!(
    validation_hotpaths,
    syntax_validation_hotpaths,
    retry_pipeline_hotpath
);
criterion_main!(validation_hotpaths);
