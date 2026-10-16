#include "app/application.h"

int main(int argc, char** argv)
{
    Application application;
    if (!application.Initialize())
        return 1;
    if (argc > 1)
        application.Open(argv[1]);
    application.Run();
    return 0;
}
