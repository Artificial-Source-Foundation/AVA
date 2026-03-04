use crate::edit::error::EditError;
use crate::edit::fuzzy_match::FuzzyMatchStrategy;
use crate::edit::request::EditRequest;
use crate::edit::strategies::{
    EditStrategy, ExactMatchStrategy, FlexibleMatchStrategy, RegexMatchStrategy,
};

pub trait SelfCorrector {
    fn correct(&self, request: &EditRequest) -> Option<EditRequest>;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecoveryResult {
    pub content: String,
    pub strategy: String,
    pub tier: u8,
    pub used_self_correction: bool,
}

#[derive(Debug, Default)]
pub struct RecoveryPipeline {
    exact: ExactMatchStrategy,
    flexible: FlexibleMatchStrategy,
    regex: RegexMatchStrategy,
    fuzzy: FuzzyMatchStrategy,
}

impl RecoveryPipeline {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn recover(&self, request: &EditRequest) -> Result<RecoveryResult, EditError> {
        self.recover_internal(request, false)
    }

    pub fn recover_with_corrector(
        &self,
        request: &EditRequest,
        corrector: &dyn SelfCorrector,
    ) -> Result<RecoveryResult, EditError> {
        if let Ok(result) = self.recover_internal(request, false) {
            return Ok(result);
        }

        let Some(corrected) = corrector.correct(request) else {
            return Err(EditError::NoMatch);
        };
        self.recover_internal(&corrected, true)
    }

    fn recover_internal(
        &self,
        request: &EditRequest,
        used_self_correction: bool,
    ) -> Result<RecoveryResult, EditError> {
        if let Some(content) = self.exact.apply(request)? {
            return Ok(RecoveryResult {
                content,
                strategy: self.exact.name().to_string(),
                tier: 1,
                used_self_correction,
            });
        }
        if let Some(content) = self.flexible.apply(request)? {
            return Ok(RecoveryResult {
                content,
                strategy: self.flexible.name().to_string(),
                tier: 2,
                used_self_correction,
            });
        }
        if let Some(content) = self.regex.apply(request)? {
            return Ok(RecoveryResult {
                content,
                strategy: self.regex.name().to_string(),
                tier: 3,
                used_self_correction,
            });
        }
        if let Some(content) = self.fuzzy.apply(request)? {
            return Ok(RecoveryResult {
                content,
                strategy: self.fuzzy.name().to_string(),
                tier: 4,
                used_self_correction,
            });
        }
        Err(EditError::NoMatch)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct BasicCorrector;

    impl SelfCorrector for BasicCorrector {
        fn correct(&self, request: &EditRequest) -> Option<EditRequest> {
            if request.old_text.is_empty() {
                let mut corrected = request.clone();
                corrected.old_text = "abc".to_string();
                return Some(corrected);
            }
            let mut corrected = request.clone();
            corrected.old_text = request.old_text.replace([' ', '-'], "");
            Some(corrected)
        }
    }

    #[test]
    fn pipeline_uses_tier_order() {
        let p = RecoveryPipeline::new();
        let req = EditRequest::new("hello world", "world", "ava");
        let out = p.recover(&req).unwrap();
        assert_eq!(out.tier, 1);
        assert_eq!(out.strategy, "exact_match");
    }

    #[test]
    fn pipeline_reaches_fuzzy_tier() {
        let p = RecoveryPipeline::new();
        let req = EditRequest::new("let total = count + 1;", "count +1", "count + 2");
        let out = p.recover(&req).unwrap();
        assert_eq!(out.tier, 4);
        assert_eq!(out.strategy, "fuzzy_match");
    }

    #[test]
    fn pipeline_uses_corrector_when_needed() {
        let p = RecoveryPipeline::new();
        let req = EditRequest::new("abc", "", "xyz");
        let out = p.recover_with_corrector(&req, &BasicCorrector).unwrap();
        assert!(out.used_self_correction);
    }
}
