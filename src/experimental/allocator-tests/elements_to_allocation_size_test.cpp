#include "sys.h"
#include "utils/nearest_power_of_two.h"
#include "utils/is_power_of_two.h"
#include "utils/log2.h"
#include <iostream>
#include <cstdint>
#include <cstdint>
#include <algorithm>
#include "debug.h"

using allocation_size_type = std::uint_fast32_t;

// Function under test.
allocation_size_type elements_to_allocation_size(allocation_size_type smallest_allocation, std::size_t bytes)
{
  std::size_t const units = bytes / smallest_allocation + (bytes % smallest_allocation != 0);
  return smallest_allocation * utils::nearest_power_of_two(units);
}

void elements_to_allocation_size_test()
{
  for (allocation_size_type smallest_allocation = 1; smallest_allocation < 128; ++smallest_allocation)
  {
    for (std::size_t bytes = 0; bytes < 20000; ++bytes)
    {
      allocation_size_type allocation_size = elements_to_allocation_size(smallest_allocation, bytes);
      if (bytes == 0)
      {
        ASSERT(allocation_size == 0);
        continue;
      }
      // Must be greater or equal smallest_allocation.
      ASSERT(allocation_size >= smallest_allocation);
      // Must be a multiple of smallest_allocation.
      ASSERT(allocation_size % smallest_allocation == 0);
      std::size_t power_of_two = allocation_size / smallest_allocation;
      // That multiple must be a power of two.
      ASSERT(utils::is_power_of_two(power_of_two));
      int N = utils::log2(power_of_two);
      // allocation_size = smallest_allocation * 2^N where N is the smallest non-negative integral value such that bytes <= allocation_size
      ASSERT(N >= 0);
      ASSERT(allocation_size == smallest_allocation * (allocation_size_type{1} << N));
      ASSERT(bytes <= allocation_size);
      ASSERT(N == 0 || bytes > smallest_allocation * (allocation_size_type{1} << (N - 1)));
    }
  }
}

// Function under test.
allocation_size_type MaxToLargest(allocation_size_type mpp_block_size, allocation_size_type smallest_allocation)
{
  int N = utils::log2(mpp_block_size / smallest_allocation);
  // smallest_allocation must fit at least twice inside mpp_block_size
  ASSERT(N > 0);
  allocation_size_type largest_allocation = (allocation_size_type{1} << (N - 1)) * smallest_allocation;
  return largest_allocation;
}

void MaxToLargest_test()
{
  for (allocation_size_type smallest_allocation = 1; smallest_allocation < 128; ++smallest_allocation)
  {
    for (allocation_size_type mpp_block_size = 2 * smallest_allocation; mpp_block_size <= 32768; ++mpp_block_size)
    {
      allocation_size_type largest_allocation = MaxToLargest(mpp_block_size, smallest_allocation);
      // Must be greater or equal smallest_allocation.
      ASSERT(largest_allocation >= smallest_allocation);
      // Must be a multiple of smallest_allocation.
      ASSERT(largest_allocation % smallest_allocation == 0);
      std::size_t power_of_two = largest_allocation / smallest_allocation;
      // That multiple must be a power of two.
      ASSERT(utils::is_power_of_two(power_of_two));
      int N = utils::log2(power_of_two);
      // The demand is that largest_allocation = smallest_allocation * 2^N, where N is the largest possible
      // integral value such that mpp_block_size / largest_allocation >= 2.
      ASSERT(largest_allocation == smallest_allocation * (allocation_size_type{1} << N));
      ASSERT(mpp_block_size / largest_allocation >= 2);
      ASSERT(mpp_block_size / (largest_allocation * 2) < 2);
    }
  }
}

int main()
{
  elements_to_allocation_size_test();
  MaxToLargest_test();
}
