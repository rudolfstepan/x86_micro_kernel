#include "rtc.h"
#include "io.h"


#define CMOS_ADDRESS        0x70
#define CMOS_DATA           0x71

// RTC Registers
#define RTC_SECONDS         0x00
#define RTC_MINUTES         0x02
#define RTC_HOURS           0x04
#define RTC_WEEKDAY         0x06
#define RTC_DAY_OF_MONTH    0x07
#define RTC_MONTH           0x08
#define RTC_YEAR            0x09
#define RTC_CENTURY         0x32  // This register might not be present on all systems
#define RTC_STATUS_A        0x0A
#define RTC_STATUS_B        0x0B

// Function to check if CMOS is updating
int cmos_update_in_progress() {
    outb(CMOS_ADDRESS, RTC_STATUS_A);
    return (inb(CMOS_DATA) & 0x80);
}

// Converts a binary number to BCD
unsigned char bin_to_bcd(int val) {
    return (val / 10 * 16) + (val % 10);
}

// Converts a BCD value to binary
int bcd_to_bin(int val) {
    return (val / 16 * 10) + (val % 16);
}

int readFromCMOS(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

// Function to get the current date
void getDate(int* year, int* month, int* day) {
    // Wait until CMOS is not updating
    while (cmos_update_in_progress());

    outb(CMOS_ADDRESS, RTC_YEAR);
    *year = bcd_to_bin(inb(CMOS_DATA));
    outb(CMOS_ADDRESS, RTC_CENTURY);
    *year += bcd_to_bin(inb(CMOS_DATA)) * 100;
    outb(CMOS_ADDRESS, RTC_MONTH);
    *month = bcd_to_bin(inb(CMOS_DATA));
    outb(CMOS_ADDRESS, RTC_DAY_OF_MONTH);
    *day = bcd_to_bin(inb(CMOS_DATA));
}

// Function to get the current time
void getTime(int* hours, int* minutes, int* seconds) {
    // Wait until CMOS is not updating
    while (cmos_update_in_progress());

    outb(CMOS_ADDRESS, RTC_HOURS);
    *hours = bcd_to_bin(inb(CMOS_DATA));
    outb(CMOS_ADDRESS, RTC_MINUTES);
    *minutes = bcd_to_bin(inb(CMOS_DATA));
    outb(CMOS_ADDRESS, RTC_SECONDS);
    *seconds = bcd_to_bin(inb(CMOS_DATA));
}


// Function to set the date
void setDate(int year, int month, int day) {
    // Wait until CMOS is not updating
    while (cmos_update_in_progress());

    outb(CMOS_ADDRESS, RTC_YEAR);
    outb(CMOS_DATA, bin_to_bcd(year % 100));
    outb(CMOS_ADDRESS, RTC_CENTURY);
    outb(CMOS_DATA, bin_to_bcd(year / 100));
    outb(CMOS_ADDRESS, RTC_MONTH);
    outb(CMOS_DATA, bin_to_bcd(month));
    outb(CMOS_ADDRESS, RTC_DAY_OF_MONTH);
    outb(CMOS_DATA, bin_to_bcd(day));
}

// Function to set the time
void setTime(int hours, int minutes, int seconds) {
    // Wait until CMOS is not updating
    while (cmos_update_in_progress());

    outb(CMOS_ADDRESS, RTC_HOURS);
    outb(CMOS_DATA, bin_to_bcd(hours));
    outb(CMOS_ADDRESS, RTC_MINUTES);
    outb(CMOS_DATA, bin_to_bcd(minutes));
    outb(CMOS_ADDRESS, RTC_SECONDS);
    outb(CMOS_DATA, bin_to_bcd(seconds));
}