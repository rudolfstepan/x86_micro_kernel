// #include "drivers/rtc/rtc.h"
//#include "toolchain/stdio.h"

int main() {
    //clear_screen(BLACK);
    printf("Current date/time:\n");

    int year, month, day, hour, minute, second;
    read_date(&year, &month, &day);
    read_time(&hour, &minute, &second);

    printf("Date Time: %d-%d-%d %d:%d:%d\n", year, month, day, hour, minute, second);

    

    return 0;
}