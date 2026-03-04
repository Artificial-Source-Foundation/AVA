use ava_validator::{CompilationValidator, SyntaxValidator, ValidationPipeline, Validator};

fn assert_valid_or_detailed_invalid(result: ava_validator::ValidationResult) {
    assert!(result.valid || (result.error.is_some() && !result.details.is_empty()));
}

#[test]
fn validator_paths_handle_mixed_delimiter_noise_without_panicking() {
    let syntax = SyntaxValidator;
    let compilation = CompilationValidator;
    let pipeline = ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator);

    for source in [
        "([<{ random >>> <<<??",
        "fn main(){ let s = \"}}}\";",
        "]]] {{{",
        "noise\n<<<<<<< ours\n=======\n>>>>>>> theirs",
        "{{[[(()]]}} trailing >>> noise",
    ] {
        let syntax_result = std::panic::catch_unwind(|| syntax.validate(source))
            .expect("syntax validator should not panic");
        assert_valid_or_detailed_invalid(syntax_result);

        let compilation_result = std::panic::catch_unwind(|| compilation.validate(source))
            .expect("compilation validator should not panic");
        assert_valid_or_detailed_invalid(compilation_result);

        let pipeline_result =
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| pipeline.validate(source)))
                .expect("pipeline should not panic");
        assert_valid_or_detailed_invalid(pipeline_result);
    }
}
