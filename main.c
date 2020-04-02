#include <stdio.h>

#include "init.h"

#include "common.h"

#ifdef PROPERTY_UNIT_TEST
#include "utils/property.h"
#endif

int main(int argc, char *argv[])
{
    int result = NO_ERR;

    #ifdef PROPERTY_UNIT_TEST
	result = property_unit_test_main(argc, argv);
    #else
    result = init_main(argc, argv);
    #endif

    return result;
}
