/**
 * @file drivers/char/rtc.h
 * @brief CMOS-RTC-Lesevertrag.
 *
 * Layer: Ring-0 platform I/O driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Zeitfelder werden aus einem stabilen, konsistenten Snapshot geliefert.
 */
#ifndef RTC_H
#define RTC_H


extern void read_date(int* year, int* month, int* day);
extern void read_time(int* hours, int* minutes, int* seconds);

extern void write_date(int year, int month, int day);
extern void write_time(int hours, int minutes, int seconds);

#endif // RTC_H
