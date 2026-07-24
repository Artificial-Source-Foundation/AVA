#include "sys.h"
#include "ava/app/Application.h"
#include "ava/app/app.h"

#include "debug.h"

int main(int argc, char** argv)
{
  Debug(NAMESPACE_DEBUG::init());
  ava::app::Application application;
  application.initialize(argc, argv);
  return ava::app::run(argc, argv);
}
