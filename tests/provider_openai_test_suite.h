#pragma once
#include "ava/http/transport.h"
#include "ava/provider/openai_provider.h"

#include <optional>

namespace ava::tests::provider_openai_suite {

std::optional<ava::http::HttpRequest> exercise_contract_request_serialization(ava::provider::OpenAIProvider const& provider);
void exercise_contract_sse_validation(ava::provider::OpenAIProvider const& provider);
void exercise_contract_http_retry(ava::provider::OpenAIProvider const& provider);
void exercise_contract_terminal_parsing(ava::provider::OpenAIProvider const& provider);
void exercise_contract_final_transport(std::optional<ava::http::HttpRequest> const& request);

void test_openai_provider_contract();
void test_openai_ordered_output_capture();
void test_v4_ordered_turn_persistence_replay_and_openai_serialization();
void test_openai_documented_message_reconciliation();
void test_openai_non_stream_output_order_and_strictness();
void test_openai_documented_function_completion_validation();
void test_openai_stream_bridge_hides_internal_text_lifecycle_events();
void test_openai_incremental_sse_parser();
void test_openai_compatible_provider_contract();
void test_openai_compatible_parsing();
void test_builtin_openai_compatible_provider_contracts();
void test_openai_responses_refusal_and_unsupported_output();
void test_closed_provider_finish_reason_catalog();
void test_openai_provider_parser_budgets();
void test_builtin_provider_registry();

}  // namespace ava::tests::provider_openai_suite
