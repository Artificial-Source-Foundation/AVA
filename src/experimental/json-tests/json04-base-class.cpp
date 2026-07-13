#include "sys.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <array>
#include <list>
#include <vector>
#include <set>
#include "debug.h"
#if CWDEBUG
#include <libcwd/buf2str.h>
#endif

using Json = nlohmann::json;

// clang-format off

namespace ava::app::rpc::json {

// Common response header shared by every RPC reply.
// Members are private with trailing underscores; the NLOHMANN_DEFINE_*_WITH_NAMES
// variants below map each member to an underscore-free JSON key so the trailing
// underscore never leaks into the serialized representation.
class Response
{
 private:
  std::string id_;
  std::string type_;
  bool success_{};

 public:
  Response() = default;
  Response(Response&&) = default;
  Response& operator=(Response&&) = default;

  // Read accessors are provided for the fields main() needs; serialization is
  // handled by the intrusive macro below.
  std::string const& id() const { return id_; }
  std::string const& type() const { return type_; }
  bool success() const { return success_; }

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Response, "id", id_, "type", type_, "success", success_);
};

// Successful-result payload: a single integer, serialized under the JSON key "value".
class Result
{
 private:
  int result_value_{42};

 public:
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Result, "value", result_value_);
};

// Failure payload: a single message, serialized under the JSON key "value".
class Error
{
 private:
  std::string error_value_{"noooo!"};

 public:
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Error, "value", error_value_);
};

// A success response flattens the Response header together with a Result payload.
// The base fields are serialized via Response's conversion; the derived member is
// serialized under the JSON key "result".
class SuccessResponse : public Response
{
 private:
  Result result_;

 public:
  SuccessResponse(Response&& base, Result&& res) : Response(std::move(base)), result_(std::move(res)) { }

  NLOHMANN_DEFINE_DERIVED_TYPE_INTRUSIVE_WITH_NAMES(SuccessResponse, Response, "result", result_);
};

// A failure response flattens the Response header together with an Error payload,
// serialized under the JSON key "error".
class FailureResponse : public Response
{
 private:
  Error error_;

 public:
  FailureResponse(Response&& base, Error&& res) : Response(std::move(base)), error_(std::move(res)) { }

  NLOHMANN_DEFINE_DERIVED_TYPE_INTRUSIVE_WITH_NAMES(FailureResponse, Response, "error", error_);
};

} // namespace ava::app::rpc::json

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  std::array<Json, 2> json_objects = {{
    {
      {"id", "some id"},
      {"type", "some type"},
      {"success", true},
      {"result", {{"value", 13}}}
    },
    {
      {"id", "some id"},
      {"type", "some type"},
      {"success", false},
      {"error", {{"value", "This is an error"}}}
    }
  }};

  using namespace ava::app::rpc;
  std::array<json::Response, 2> r;

  for (int i = 0; i < 2; ++i)
  {
    std::unique_ptr<json::Response> base;
    bool is_success_response{};

    {
      // Read common header.
      r[i] = std::move(json_objects[i].get<json::Response>());
      is_success_response = r[i].success();

      if (is_success_response)
        base = std::make_unique<json::SuccessResponse>(std::move(r[i]), json_objects[i].at("result").get<json::Result>());
      else
        base = std::make_unique<json::FailureResponse>(std::move(r[i]), json_objects[i].at("error").get<json::Error>());
    }

    // Convert base back to JSON object for inspection.
    Json j;
    if (is_success_response)
      j = static_cast<json::SuccessResponse const&>(*base);
    else
      j = static_cast<json::FailureResponse const&>(*base);

    Dout(dc::notice, "r[" << i << "] = " << std::setw(4) << j);
  }
}
