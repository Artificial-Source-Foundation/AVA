#include "sys.h"
#include "ava/app/app.h"
#include "debug.h"

int main(int argc, char** argv)
{
  Debug(NAMESPACE_DEBUG::init());
  return ava::app::run(argc, argv);
}
