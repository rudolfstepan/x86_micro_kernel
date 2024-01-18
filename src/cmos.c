#include "cmos.h"
#include "io.h"


#define CMOS_ADDRESS        0x70
#define CMOS_DATA           0x71
#define RTC_SECONDS         0x00
#define RTC_MINUTES         0x02
#define RTC_HOURS           0x04
#define RTC_WEEKDAY         0x06
#define RTC_DAY_OF_MONTH    0x07
#define RTC_MONTH           0x08
#define RTC_YEAR            0x09
#define RTC_CENTURY         0x32  // This register might not be present on all systems
#define STATUS_REGISTER_A   0x0A
#define STATUS_REGISTER_B   0x0B
#define STATUS_REGISTER_C   0x0C
#define STATUS_REGISTER_D   0x0D


int bcdToBinary(int bcdValue) {
    return ((bcdValue / 16) * 10) + (bcdValue % 16);
}

int readFromCMOS(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

void getDate(int* year, int* month, int* day) {
    *year = readFromCMOS(RTC_YEAR) + readFromCMOS(RTC_CENTURY) * 100;
    int statusB = readFromCMOS(STATUS_REGISTER_B);
    int isBCD = !(statusB & 0x04);

    if (isBCD) {
        *year = bcdToBinary(readFromCMOS(RTC_YEAR) + readFromCMOS(RTC_CENTURY) * 100);
    }

    *month = readFromCMOS(RTC_MONTH);
    *day = readFromCMOS(RTC_DAY_OF_MONTH);
}

void getTime(int* hours, int* minutes, int* seconds) {
    *hours = readFromCMOS(RTC_HOURS);
    *minutes = readFromCMOS(RTC_MINUTES);
    *seconds = readFromCMOS(RTC_SECONDS);
}