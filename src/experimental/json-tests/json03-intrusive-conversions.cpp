#include "sys.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
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

struct Answer
{
  int everything;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Answer, everything);

class Object
{
 private:
  std::string currency_;
  double value_;

 public:
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Object,
    "currency", currency_,
    "value", value_
  );
};

struct Empty
{
};

// Serialize an Empty value as an empty JSON object ({}) and read it back from one.
// The NLOHMANN_DEFINE_TYPE_* macros cannot be used for a type with no members because
// they generate per-member get_to()/assignment calls, so the conversion is provided
// manually as free functions. Templated on BasicJsonType to interoperate with any
// basic_json instantiation, matching the convention used by the library's own macros.
template <typename BasicJsonType>
void to_json(BasicJsonType& j, Empty const&)
{
  j = BasicJsonType::object();
}

// Read an Empty value from JSON.
//
// Any JSON object is accepted (Empty carries no data to populate); a non-object
// value is rejected with a type error so that a mismatched representation is not
// silently swallowed.
template <typename BasicJsonType>
void from_json(BasicJsonType const& j, Empty&)
{
  if (!j.is_object())
    throw nlohmann::json::type_error::create(302, "expected JSON object for Empty", &j);
}

class Test
{
 private:
  double pi_;
  bool happy_;
  std::string name_;
  Answer answer_;
  std::list<int> list_;
  Object object_;
  std::vector<double> emptyarray_;
  Empty emptyobject_;

 public:
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_NAMES(Test,
    "pi", pi_,
    "happy", happy_,
    "name", name_,
    "answer", answer_,
    "list", list_,
    "object", object_,
    "emptyarray", emptyarray_,
    "emptyobject", emptyobject_
  );
};

} // namespace ava::app::rpc::json

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  Json const j2 = {
    {"pi", 3.141},
    {"happy", true},
    {"name", "Niels"},
    {"answer", {
      {"everything", 42}
    }},
    {"list", {1, 0, 2}},
    {"object", {
      {"currency", "USD"},
      {"value", 42.99}
    }},
    {"emptyarray", Json::array()},
    {"emptyobject", Json::object()}
  };

  // Initialize Test from JSON object.
  auto test = j2.get<ava::app::rpc::json::Test>();

  // Convert Test back to JSON object.
  Json j3 = test;

  Dout(dc::notice, "j3 = " << std::setw(4) << j3);
}
