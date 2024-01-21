#include "cmos.h"
#include "stdio.h"


int start(void) {
    //clear_screen(BLACK);
    printf("Reading Datetime from system:\n");

    int year, month, day, hour, minute, second;
    getDate(&year, &month, &day);
    getTime(&hour, &minute, &second);

    printf("Date Time: %d-%d-%d %d:%d:%d\n", year, month, day, hour, minute, second);

    return 0;
}