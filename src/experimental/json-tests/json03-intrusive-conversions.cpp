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

  Test test = j2.get<Test>();
}
