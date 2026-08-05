#include "sys.h"
#include "ava/app/Application.h"
#include "ava/app/app.h"
#include "ava/app/ava_debug.h"

#include "debug.h"

int main(int argc, char** argv)
{
  Debug(ava::app::initialize_debug());
  ava::app::Application application;
  application.initialize(argc, argv);
  return ava::app::run(argc, argv);
}
