#include "sys.h"
#include "ava/app/Application.h"
#include "memory/MemoryPagePool.h"
#include "memory/NodeMemoryResource.h"
#include "utils/is_power_of_two.h"
#include "utils/nearest_power_of_two.h"
#include <iostream>
#include <concepts>
#include <memory>
#include <type_traits>
#include <vector>
#include "debug.h"

// We assume that the size of every std::vector is the same, independent of what type it stores.
// In fact, this value is expected to be three pointers: 24 bytes.
static constexpr std::size_t element_size = sizeof(std::vector<int>);
static constexpr std::size_t smallest_allocation = utils::nearest_power_of_two(element_size);
static constexpr std::size_t largest_allocation = ava::app::Application::mpp_page_size;

// Because we only want to allocate a power of two bytes at a time, there is a relationship
// between the number of elements and the size of the allocated memory required for those.
constexpr unsigned int allocation_size_to_elements(unsigned int size)
{
  ASSERT(utils::is_power_of_two(size));
  return size / element_size;

}

// And the inverse of that.
constexpr unsigned int elements_to_allocation_size(std::size_t elements)
{
  return utils::nearest_power_of_two(elements * element_size);
}

constexpr int allocation_size_to_node_memory_resource_index(unsigned int size)
{
  ASSERT(utils::is_power_of_two(size));
  ASSERT(smallest_allocation <= size && size <= largest_allocation);
  return utils::log2(size) - utils::log2(smallest_allocation);
}

static constexpr std::size_t number_of_allocation_sizes = allocation_size_to_node_memory_resource_index(largest_allocation) + 1;
static constexpr std::size_t maximum_number_of_elements = allocation_size_to_elements(largest_allocation);

// Allocate std::vector<T> objects for an outer std::vector.
//
template <typename T>
class VectorVectorAllocator
{
 public:
  using value_type = std::vector<T>;
  using pointer = value_type*;
  using size_type = std::size_t;

 private:
  memory::MemoryPagePool& mmp_;
  static std::array<memory::NodeMemoryResource, number_of_allocation_sizes> nmrs_;

 public:
  VectorVectorAllocator(memory::MemoryPagePool& mmp) : mmp_(mmp) { }

  template <typename U>
  struct rebind
  {
    static_assert(std::same_as<U, value_type>, "VectorVectorAllocator can allocate only its inner-vector type");
    using other = VectorVectorAllocator;
  };

  // Allocate uninitialized storage for n value_type objects and report its byte count.
  //
  // std::allocator supplies the required overflow checking and alignment guarantees.
  value_type* allocate(std::size_t n)
  {
    std::size_t const allocation_size = elements_to_allocation_size(n);
    std::cout << "Allocating " << allocation_size << " bytes; use index " << allocation_size_to_node_memory_resource_index(allocation_size) << std::endl;
    return std::allocator<value_type>{}.allocate(allocation_size);
  }

  std::allocation_result<pointer, size_type> allocate_at_least(std::size_t n)
  {
    std::size_t const allocation_size = elements_to_allocation_size(n);
    std::size_t const count = allocation_size_to_elements(allocation_size);
    return {allocate(count), count};
  }

  // Release storage at p for the n objects supplied to the corresponding allocation.
  void deallocate(value_type* p, std::size_t n) noexcept
  {
    std::size_t const allocation_size = elements_to_allocation_size(n);
#if CW_DEBUG
    std::size_t const count = allocation_size_to_elements(allocation_size);
    ASSERT(elements_to_allocation_size(count) == allocation_size);
#endif
    std::cout << "Deallocating " << n << " elements: " << allocation_size << " bytes; index " << allocation_size_to_node_memory_resource_index(allocation_size) << std::endl;
    std::allocator<value_type>{}.deallocate(p, n);
  }

  constexpr size_type max_size() const
  {
    return maximum_number_of_elements;
  }
};

template <typename T>
constexpr bool operator==(VectorVectorAllocator<T> const&, VectorVectorAllocator<T> const&) noexcept
{
  return true;
}

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
  memory::MemoryPagePool mpp(0x8000);

  VectorVectorAllocator<int> alloc(mpp);

  std::vector<std::vector<int>, VectorVectorAllocator<int>> vec(alloc);

  std::cout << "Adding elements to vector:" << std::endl;
  vec.push_back(std::vector<int>{1});
  vec.push_back(std::vector<int>{2, 3});
  vec.push_back(std::vector<int>{4, 5, 6, 7});

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
