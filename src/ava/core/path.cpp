#include "sys.h"
#include "ava/core/path.h"

namespace ava::core {

std::filesystem::path normalized_absolute_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error)
    absolute = path;
  return absolute.lexically_normal();
}

}  // namespace ava::core
