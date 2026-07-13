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

  // This string MAY NOT end on a new-line.
  std::string const json_example = "{\"id\":\"proto\",\"type\":\"response\",\"success\":true,\"result\":{\"protocol_version\":1,\"supported_protocol_versions\":[1],\"session_entry_version\":3,\"supported_session_entry_versions\":[0,1,2,3]}}";

  Dout(dc::notice, "input = " << buf2str(json_example.data(), json_example.length()));

  std::stringstream ss;
  ss << json_example;

  Json data = Json::parse(ss);

  Dout(dc::notice, "parsed: " << std::setw(4) << data);
}
