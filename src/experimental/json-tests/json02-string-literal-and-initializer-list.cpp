#include "sys.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "debug.h"
#if CWDEBUG
#include <libcwd/buf2str.h>
#endif

using Json = nlohmann::json;

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  // Using user-defined (raw) string literals
  using namespace nlohmann::literals;

  // Construct from string-literal.
  Json const ex2 = R"(
    {
      "pi": 3.141,
      "happy": true
    }
  )"_json;

  Dout(dc::notice, "ex2 = " << ex2);

  // Construct from initializer list.
  Json const j2 = {
    {"pi", 3.141},
    {"happy", true},
    {"name", "Niels"},
    {"nothing", nullptr},
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

  Dout(dc::notice, "j2 = " << std::setw(4) << j2);

  std::string j2str = j2.dump();
  Dout(dc::notice, "j2str = " << j2str);
}
