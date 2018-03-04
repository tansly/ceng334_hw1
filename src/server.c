#include "globals.h"
#include "map.h"

#include <stdio.h>

int main(void)
{
    init_map();
    run_simulation();
    clean_map();
    return 0;
}
