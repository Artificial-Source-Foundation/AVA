#include "sys.h"
#include "ava/core/path.h"

#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::core {

std::filesystem::path normalized_absolute_path(std::filesystem::path const& path)
{
  if (!path.is_absolute())
    return (logical_cwd() / path).lexically_normal();

  return path.lexically_normal();
}

}  // namespace ava::core
