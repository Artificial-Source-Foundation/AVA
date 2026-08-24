#include <cstdlib>
#include <limits>

int main()
{
  int volatile maximum = std::numeric_limits<int>::max();
  int volatile overflow = maximum + 1;
  return overflow == maximum ? EXIT_FAILURE : EXIT_SUCCESS;
}
