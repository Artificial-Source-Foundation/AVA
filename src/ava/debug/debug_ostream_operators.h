#pragma once

#include <mutex>
#include <iostream>

namespace debug::ostream_operators {

inline std::ostream& operator<<(std::ostream& os, std::mutex const& UNUSED_ARG(mutex))
{
  //FIXME
  return os;
}

inline std::ostream& operator<<(std::ostream& os, std::string const& str)
{
  // Put double quotes around strings.
  os << '"';
  os.write(str.data(), str.size());
  os << '"';
  return os;
}

template<typename T>
std::ostream& operator<<(std::ostream& os, std::optional<T> const& UNUSED_ARG(opt))
{
  //FIXME
  return os;
}

template<typename T>
concept ConceptHasToString = requires(T obj)
{
  to_string(obj);
};

// Use to_string for types in namespace vk when to_string is defined for those types.
template<ConceptHasToString T>
std::ostream& operator<<(std::ostream& os, T const& obj)
{
  os << to_string(obj);
  return os;
}

} // namespace debug::ostream_operators
