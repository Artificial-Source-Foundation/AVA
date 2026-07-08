#pragma once

#include "print_pointer.h"
#include <concepts>
#include <iostream>
#include <mutex>
#include <type_traits>

namespace debug::ostream_operators {

inline std::ostream& operator<<(std::ostream& os, std::mutex const& UNUSED_ARG(mutex))
{
  os << "$mutex$";
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
std::ostream& operator<<(std::ostream& os, std::optional<T> const& opt)
{
  if (opt.has_value())
  {
    LIBCWD_USING_OSTREAM_PRELUDE;
    os << opt.value();
  }
  else
    os << "$no value$";
  return os;
}

template<typename T>
concept ConceptHasToString = requires(T obj)
{
  to_string(obj);
};

// Use to_string for types in namespace vk when to_string is defined for those types.
template<ConceptHasToString T>
inline std::ostream& operator<<(std::ostream& os, T const& obj)
{
  os << to_string(obj);
  return os;
}

template<typename T>
concept ConceptIsNonCharPointer =
    std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, std::unique_ptr<T>> ||
    std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, std::shared_ptr<T>> ||
    std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, boost::intrusive_ptr<T>> ||
    (std::is_pointer_v<T> && // 1. T must strictly be a pointer type (no arrays!)
    !std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, char> &&
    !std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, wchar_t> &&
    !std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, char8_t> &&
    !std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, char16_t> &&
    !std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, char32_t>);

template<ConceptIsNonCharPointer T>
inline std::ostream& operator<<(std::ostream& os, T const& obj)
{
  os << ava_utils::print_pointer(obj);
  return os;
}

} // namespace debug::ostream_operators
