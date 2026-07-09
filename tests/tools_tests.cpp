#include "sys.h"
#include "tests/support/test_harness.h"

void run_tools_file_tests();
void run_tools_search_tests();
void run_tools_process_network_tests();

void run_tools_tests()
{
  run_tools_file_tests();
  run_tools_search_tests();
  run_tools_process_network_tests();
}
