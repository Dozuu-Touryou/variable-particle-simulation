#include "application.h"

int main()
{
   Application app;
   if (app.initialize())
      app.run();
   app.shutdown();
}