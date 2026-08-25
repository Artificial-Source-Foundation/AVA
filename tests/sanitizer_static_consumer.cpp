#include <array>
#include <cstddef>
#include <cstdlib>

int ava_sanitizer_contract_read(int const* values, std::size_t index);

int main()
{
  constexpr std::array values{3, 7};
  return ava_sanitizer_contract_read(values.data(), 1) == 7 ? EXIT_SUCCESS : EXIT_FAILURE;
}
