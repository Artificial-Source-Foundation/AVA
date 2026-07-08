#pragma once

#include "utils/print_pointer.h"
#include "utils/to_string.h"
#include <concepts>
#include <iostream>
#include <mutex>
#include <type_traits>
#include "debug.h"

NAMESPACE_DEBUG_START
// Allow using `os << __write__("member:")` in the generated `print_members`
// member functions to avoid getting quotes around those string literals.
struct AvaRawDebugString { char const* ptr; };
[[gnu::always_inline]] inline AvaRawDebugString __write__(char const* ptr) { return {ptr}; }
[[gnu::always_inline]] inline std::ostream& operator<<(std::ostream& os, AvaRawDebugString raw_str)
{
  os.write(raw_str.ptr, std::strlen(raw_str.ptr));
  return os;
}
NAMESPACE_DEBUG_END

namespace debug::ostream_operators {

inline std::ostream& operator<<(std::ostream& os, std::mutex const& UNUSED_ARG(mutex))
{
  os.write("$mutex$", 7);
  return os;
}

template<typename T>
inline std::ostream& operator<<(std::ostream& os, std::function<T> const& UNUSED_ARG(func))
{
  os.write("$std::function$", 15);
  return os;
}

template<typename T>
concept ConceptHasToString = requires(T t)
{
  { to_string(t) } -> std::convertible_to<std::string>;
};

template<typename T>
concept ConceptIsEnum = std::is_enum_v<T>;

template<ConceptIsEnum T>
inline std::ostream& operator<<(std::ostream& os, T const& e)
{
  if constexpr (ConceptHasToString<T>)
    os << to_string(e);
  else
    os << utils::to_string(e);
  return os;
}

template<typename T>
concept ConceptIsNonIntrusiveNonArrayPointer =
    std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, std::unique_ptr<T>> ||
    std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, std::shared_ptr<T>> ||
    std::is_pointer_v<T>;

template<ConceptIsNonIntrusiveNonArrayPointer T>
inline std::ostream& operator<<(std::ostream& os, T const& obj)
{
  utils::operator<<(os, utils::print_pointer(obj));
  return os;
}

// boost::intrusive_ptr defines its own operator<< as
// template<class E, class T, class Y> std::basic_ostream<E, T> & operator<< (std::basic_ostream<E, T> & os, intrusive_ptr<Y> const & p)
// In order to override that we use a constrained template to win over the unconstrained one:
template<class E, class T, class Y>
requires (sizeof(E) <= sizeof(char32_t)) // Expected to be always true, but it counts as a constrain.
std::basic_ostream<E, T>& operator<<(std::basic_ostream<E, T>& os, boost::intrusive_ptr<Y> const& p)
{
  utils::operator<< <Y>(os, utils::print_pointer(p));
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

inline std::ostream& operator<<(std::ostream& os, char const* str)
{
  os << debug::print_string(str);
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
    os.write("$no value$", 10);
  return os;
}

} // namespace debug::ostream_operators
