#include "sys.h"
#include "memory/MemoryPagePool.h"
#include "memory/VectorAllocator.h"

#include <iostream>
#include <vector>
#include "debug.h"

std::ostream& operator<<(std::ostream& os, std::vector<int> const& v)
{
  os << '{';
  char const* separator = "";
  for (int i : v)
  {
    os << separator << i;
    separator = ", ";
  }
  os << '}';
  return os;
}

int main()
{
  Debug(NAMESPACE_DEBUG::init());
  Dout(dc::notice, "Debug output is turned on.");

  memory::MemoryPagePool mpp;
  using element_type = std::vector<int>;
  using VectorAllocator = memory::VectorAllocator<element_type, 24>;
  VectorAllocator alloc(mpp);
  using vec_type = std::vector<element_type, VectorAllocator>;
  vec_type vec(alloc);
//  vec.reserve(vec_type::allocator_type::optimal_capacity(VectorAllocator::maximum_number_of_elements));

  std::cout << "Adding elements to vector:" << std::endl;
  for (int i = 0; i < 2 * VectorAllocator::maximum_number_of_elements; ++i)
    vec.push_back(element_type{i});

  std::cout << "Vector contents: ";
  for (element_type const& v : vec)
  {
    std::cout << v << " ";
  }
  std::cout << std::endl;

  std::cout << "Resizing vector to 1 element:" << std::endl;
  vec.resize(1);

  std::cout << "Clearing vector:" << std::endl;
  vec.clear();

  ASSERT(mpp.block_size() / sizeof(element_type) == VectorAllocator::maximum_number_of_elements);

  auto p0 = alloc.allocate(0);
  auto p1 = alloc.allocate(1);
  auto p2 = alloc.allocate(mpp.block_size() / sizeof(element_type));
  std::size_t oc0 = vec_type::allocator_type::optimal_capacity(0);
  std::size_t oc1 = vec_type::allocator_type::optimal_capacity(1);
  std::size_t oc2 = vec_type::allocator_type::optimal_capacity(mpp.block_size() / sizeof(element_type));

  VectorAllocator::rebind<char>::other alloc2(alloc);
  std::vector<char, VectorAllocator::rebind<char>::other> vec2(alloc2);
  vec2.reserve(11);

  Dout(dc::notice, "Leaving main()...");
}
