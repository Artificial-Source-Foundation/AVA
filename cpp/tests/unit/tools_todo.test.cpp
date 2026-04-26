#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "ava/tools/todo_tools.hpp"

TEST_CASE("todo tools share process-local checklist state", "[ava_tools]") {
  auto state = std::make_shared<ava::tools::TodoListState>();
  ava::tools::TodoWriteTool todo_write(state);
  ava::tools::TodoReadTool todo_read(state);

  const auto write_result = todo_write.execute(nlohmann::json{{"todos",
                                                               nlohmann::json::array(
                                                                   {
                                                                       nlohmann::json{{"content", "Implement feature"},
                                                                                      {"status", "in_progress"},
                                                                                      {"priority", "high"}},
                                                                        nlohmann::json{{"content", "Write tests"},
                                                                                       {"status", "pending"},
                                                                                       {"priority", "medium"}},
                                                                        nlohmann::json{{"content", "Done"},
                                                                                       {"status", "completed"},
                                                                                       {"priority", "low"}},
                                                                        nlohmann::json{{"content", "Cancelled"},
                                                                                       {"status", "cancelled"},
                                                                                       {"priority", "low"}},
                                                                    }
                                                                )}});
  REQUIRE_FALSE(write_result.is_error);
  REQUIRE(write_result.content.find("4 total") != std::string::npos);
  REQUIRE(write_result.content.find("2 incomplete") != std::string::npos);

  const auto read_result = todo_read.execute(nlohmann::json::object());
  REQUIRE_FALSE(read_result.is_error);
  REQUIRE(read_result.content.find("Implement feature") != std::string::npos);
  REQUIRE(read_result.content.find("2 incomplete") != std::string::npos);

  const auto replace_result = todo_write.execute(
      nlohmann::json{{"todos",
                      nlohmann::json::array(
                          {
                              nlohmann::json{{"content", "Ship"}, {"status", "completed"}, {"priority", "low"}},
                          }
                      )}}
  );
  REQUIRE_FALSE(replace_result.is_error);

  const auto replaced_read = todo_read.execute(nlohmann::json::object());
  REQUIRE(replaced_read.content.find("Ship") != std::string::npos);
  REQUIRE(replaced_read.content.find("Implement feature") == std::string::npos);
  REQUIRE(replaced_read.content.find("0 incomplete") != std::string::npos);
}

TEST_CASE("todo tools validate required fields and content bounds", "[ava_tools]") {
  auto state = std::make_shared<ava::tools::TodoListState>();
  ava::tools::TodoWriteTool todo_write(state);
  ava::tools::TodoReadTool todo_read(state);

  REQUIRE_THROWS(todo_write.execute(nlohmann::json::object()));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos", "not-array"}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos", nlohmann::json::array({nlohmann::json::object()})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"status", "pending"},
                                                                                            {"priority", "high"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", "x"},
                                                                                            {"priority", "high"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", "x"},
                                                                                            {"status", "pending"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", 1},
                                                                                            {"status", "pending"},
                                                                                            {"priority", "high"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", "x"},
                                                                                            {"status", false},
                                                                                            {"priority", "high"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", "x"},
                                                                                            {"status", "pending"},
                                                                                            {"priority", nullptr}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", "x"},
                                                                                            {"status", "bogus"},
                                                                                            {"priority", "high"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", "x"},
                                                                                            {"status", "pending"},
                                                                                            {"priority", "urgent"}}})}}));
  REQUIRE_THROWS(todo_write.execute(nlohmann::json{{"todos",
                                                    nlohmann::json::array({nlohmann::json{{"content", std::string(8193, 'x')},
                                                                                            {"status", "pending"},
                                                                                            {"priority", "high"}}})}}));

  const auto max_content_write = todo_write.execute(nlohmann::json{{"todos",
                                                                    nlohmann::json::array({nlohmann::json{
                                                                        {"content", std::string(8192, 'x')},
                                                                        {"status", "pending"},
                                                                        {"priority", "high"},
                                                                    }})}});
  REQUIRE_FALSE(max_content_write.is_error);

  const auto empty_write = todo_write.execute(nlohmann::json{{"todos", nlohmann::json::array()}});
  REQUIRE(empty_write.content.find("0 total") != std::string::npos);
  REQUIRE(empty_write.content.find("0 incomplete") != std::string::npos);
  REQUIRE(todo_read.execute(nlohmann::json::object()).content.find("No todos") != std::string::npos);
}

TEST_CASE("todo tools reject null shared state", "[ava_tools]") {
  REQUIRE_THROWS(ava::tools::TodoWriteTool(nullptr));
  REQUIRE_THROWS(ava::tools::TodoReadTool(nullptr));
}
