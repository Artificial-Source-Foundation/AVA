#include "sys.h"
#include "memory/MemoryPagePool.h"
#include "memory/GeometricAllocator.h"

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
  using VectorAllocator = memory::VectorAllocator<std::vector<int>>;
  VectorAllocator alloc(mpp);
  using vec_type = std::vector<std::vector<int>, VectorAllocator>;
  vec_type vec(alloc);
//  vec.reserve(vec_type::allocator_type::optimal_capacity(VectorAllocator::maximum_number_of_elements));

  std::cout << "Adding elements to vector:" << std::endl;
  for (int i = 0; i < 2 * VectorAllocator::maximum_number_of_elements; ++i)
    vec.push_back(std::vector<int>{i});

  std::cout << "Vector contents: ";
  for (std::vector<int> v : vec)
  {
    std::cout << v << " ";
  }
  std::cout << std::endl;

  std::cout << "Resizing vector to 1 element:" << std::endl;
  vec.resize(1);

  std::cout << "Clearing vector:" << std::endl;
  vec.clear();
}
