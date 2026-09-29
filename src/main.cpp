#include "app/Application.hpp"


int main()
{
    Application application;


    if (!application.initialize())
    {
        return 1;
    }


    application.run();


    return 0;
}