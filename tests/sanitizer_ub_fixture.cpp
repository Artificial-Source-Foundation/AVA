#include <csignal>
#include <cstdlib>
#include <limits>

int main()
{
  std::signal(SIGABRT, [](int) { std::_Exit(EXIT_FAILURE); });
  int volatile maximum = std::numeric_limits<int>::max();
  int volatile overflow = maximum + 1;
  return overflow == maximum ? EXIT_FAILURE : EXIT_SUCCESS;
}
