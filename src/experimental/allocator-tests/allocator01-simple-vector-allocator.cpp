#include "sys.h"
#include "memory/MemoryPagePool.h"
#include "memory/NodeMemoryResource.h"
#include "utils/is_power_of_two.h"
#include "utils/nearest_power_of_two.h"
#include "utils/macros.h"
#include "ava/app/Application.h"

#include <array>
#include <concepts>
#include <iostream>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>
#include "debug.h"

// We assume that the size of every std::vector is the same, independent of what type it stores.
// In fact, this value is expected to be three pointers: 24 bytes.
static constexpr std::size_t element_size = sizeof(std::vector<int>);
// This allocator will allow only allocations to store up to `max_vectors_per_vector` elements.
static constexpr std::size_t max_vectors_per_vector = 21;       // If this is not enough then change it to 42, 85, 170, ...

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

static constexpr std::size_t smallest_allocation = utils::nearest_power_of_two(element_size);
static constexpr std::size_t largest_allocation = elements_to_allocation_size(max_vectors_per_vector);

constexpr int allocation_size_to_node_memory_resource_index(unsigned int size)
{
  ASSERT(utils::is_power_of_two(size));
  ASSERT(smallest_allocation <= size && size <= largest_allocation);
  return utils::log2(size) - utils::log2(smallest_allocation);
}

static constexpr std::size_t number_of_allocation_sizes = allocation_size_to_node_memory_resource_index(largest_allocation) + 1;
static constexpr std::size_t maximum_number_of_elements = allocation_size_to_elements(largest_allocation);
static_assert(maximum_number_of_elements == max_vectors_per_vector, "max_vectors_per_vector can be larger without using more memory");

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
  static std::array<memory::NodeMemoryResource, number_of_allocation_sizes> nmrs_;
  static std::once_flag initialize_nmrs_once_;

 public:
  // Bind the shared size-class resources to mpp the first time this allocator specialization is constructed.
  //
  // The pool must outlive every allocator and allocation of this specialization. Concurrent construction is safe;
  // the first supplied pool remains the upstream resource for all later allocator copies and constructions.
  VectorVectorAllocator(memory::MemoryPagePool& mpp)
  {
    std::call_once(initialize_nmrs_once_, [&mpp] {
      std::size_t allocation_size = smallest_allocation;
      for (memory::NodeMemoryResource& nmr : nmrs_)
      {
        // Note that this doesn't allocate any memory pages yet. That only happens once a NodeMemoryResource is first used.
        nmr.init(&mpp, allocation_size);
        allocation_size *= 2;
      }
    });
  }

  template <typename U>
  struct rebind
  {
    static_assert(std::same_as<U, value_type>, "VectorVectorAllocator can allocate only its inner-vector type");
    using other = VectorVectorAllocator;
  };

  // Allocate uninitialized storage for n value_type objects from the shared resource for its power-of-two size class.
  //
  // Throws std::bad_alloc when the upstream page pool cannot supply another block.
  value_type* allocate(std::size_t n)
  {
    // A zero-sized allocation must be supported by a conforming allocator. The result is unspecified; we choose to return nullptr.
    if (AI_UNLIKELY(n == 0))
      return nullptr;
    std::size_t const allocation_size = elements_to_allocation_size(n);
    int const index = allocation_size_to_node_memory_resource_index(allocation_size);
    if (AI_UNLIKELY(index >= nmrs_.size()))
      throw std::length_error("VectorVectorAllocator::allocate");
    std::cout << "Allocating " << allocation_size << " bytes; use index " << index << std::endl;
    void* const allocation = nmrs_[index].allocate(allocation_size);
    if (allocation == nullptr)
      throw std::bad_alloc{};
    return static_cast<value_type*>(allocation);
  }

  static std::size_t optimal_capacity(std::size_t n)
  {
    std::size_t const allocation_size = elements_to_allocation_size(n);
    return allocation_size_to_elements(allocation_size);
  }

  std::allocation_result<pointer, size_type> allocate_at_least(std::size_t n)
  {
    std::size_t const count = optimal_capacity(n);
    return {allocate(count), count};
  }

  // Return storage at p to the shared size-class resource selected by the corresponding n-object allocation.
  void deallocate(value_type* p, std::size_t n) noexcept
  {
    // This allocator returns nullptr for zero-sized allocations, therefore we need to support deallocating that.
    if (AI_UNLIKELY(p == nullptr))
      return;
    std::size_t const allocation_size = elements_to_allocation_size(n);
#if CW_DEBUG
    std::size_t const count = allocation_size_to_elements(allocation_size);
    ASSERT(elements_to_allocation_size(count) == allocation_size);
#endif
    int const index = allocation_size_to_node_memory_resource_index(allocation_size);
    std::cout << "Deallocating " << n << " elements: " << allocation_size << " bytes; index " << index << std::endl;
    nmrs_[index].deallocate(p);
  }

  constexpr size_type max_size() const { return maximum_number_of_elements; }
};

template <typename T>
std::array<memory::NodeMemoryResource, number_of_allocation_sizes> VectorVectorAllocator<T>::nmrs_;

//static
template <typename T>
std::once_flag VectorVectorAllocator<T>::initialize_nmrs_once_;

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
  Dout(dc::notice, "Debug output is turned on.");

  memory::MemoryPagePool mpp(0x8000);

  VectorVectorAllocator<int> alloc(mpp);

  using vec_type = std::vector<std::vector<int>, VectorVectorAllocator<int>>;
  vec_type vec(alloc);
//  vec.reserve(vec_type::allocator_type::optimal_capacity(7));

  std::cout << "Adding elements to vector:" << std::endl;
  vec.push_back(std::vector<int>{1});
  vec.push_back(std::vector<int>{2, 3});
  vec.push_back(std::vector<int>{4, 5, 6, 7});
  vec.push_back(std::vector<int>{8, 9, 10, 11, 12, 13, 14, 15});
  vec.push_back(std::vector<int>{16, 17, 18, 19});
  vec.push_back(std::vector<int>{20, 21});
  vec.push_back(std::vector<int>{22});

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
