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

struct Response
{
  std::string id;
  std::string type;
  bool success;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Response, id, type, success);

struct Result
{
  int result_value{42};

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Result, "value", result_value);
};

struct Error
{
  std::string error_value{"noooo!"};

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Error, "value", error_value);
};

struct SuccessResponse : public Response
{
  Result result;

  SuccessResponse(Response const& base, Result const& res) : Response(base), result(res) { }
};

NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE(SuccessResponse, Response, result);

struct FailureResponse : public Response
{
  Error error;

  FailureResponse(Response const& base, Error const& res) : Response(base), error(res) { }
};

NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE(FailureResponse, Response, error);

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
    // Read common header.
    r[i] = json_objects[i].get<json::Response>();

    std::unique_ptr<json::Response> base;
    if (r[i].success)
      base = std::make_unique<json::SuccessResponse>(r[i], json_objects[i].at("result").get<json::Result>());
    else
      base = std::make_unique<json::FailureResponse>(r[i], json_objects[i].at("error").get<json::Error>());

    // Convert base back to JSON object.
    Json j;
    if (r[i].success)
      j = static_cast<json::SuccessResponse const&>(*base);
    else
      j = static_cast<json::FailureResponse const&>(*base);
    Dout(dc::notice, "r[" << i << "] = " << std::setw(4) << j);
  }
}
