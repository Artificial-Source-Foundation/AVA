#include <cstddef>

int ava_sanitizer_contract_read(int const* values, std::size_t index)
{
  return values[index];
}
