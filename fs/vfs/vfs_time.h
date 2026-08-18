/**
 * @file fs/vfs/vfs_time.h
 * @brief Begrenzte Konvertierung zwischen FAT-Datum und Unix-Zeit.
 *
 * FAT speichert lokale Kalenderwerte mit einer Auflösung von zwei Sekunden.
 * Der VFS-Vertrag verwendet dagegen Sekunden seit 1970-01-01. Die
 * Konvertierung prüft jeden Kalenderwert, damit beschädigte Directory-
 * Einträge nicht zu Überläufen oder falschen Zeitstempeln führen.
 */
#ifndef VFS_TIME_H
#define VFS_TIME_H

#include <stdint.h>

static inline int vfs_time_is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static inline uint32_t vfs_time_days_before_year(int year) {
    uint32_t days = 0U;
    for (int cursor = 1970; cursor < year; ++cursor)
        days += vfs_time_is_leap_year(cursor) ? 366U : 365U;
    return days;
}

static inline uint32_t vfs_time_from_calendar(int year, int month, int day,
                                               int hour, int minute,
                                               int second) {
    static const uint8_t month_days[12] =
        {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    if (year < 1970 || year > 2107 || month < 1 || month > 12 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
        return 0U;
    uint32_t limit = month_days[month - 1];
    if (month == 2 && vfs_time_is_leap_year(year)) ++limit;
    if (day < 1 || (uint32_t)day > limit) return 0U;
    uint32_t days = vfs_time_days_before_year(year);
    for (int cursor = 1; cursor < month; ++cursor) {
        days += month_days[cursor - 1];
        if (cursor == 2 && vfs_time_is_leap_year(year)) ++days;
    }
    days += (uint32_t)day - 1U;
    uint64_t seconds = (uint64_t)days * 86400ULL +
                       (uint64_t)hour * 3600ULL +
                       (uint64_t)minute * 60ULL + (uint32_t)second;
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

static inline uint32_t vfs_time_from_fat(uint16_t date, uint16_t time) {
    int day = (int)(date & 0x1FU);
    int month = (int)((date >> 5) & 0x0FU);
    int year = (int)((date >> 9) & 0x7FU) + 1980;
    int second = (int)(time & 0x1FU) * 2;
    int minute = (int)((time >> 5) & 0x3FU);
    int hour = (int)((time >> 11) & 0x1FU);
    return vfs_time_from_calendar(year, month, day, hour, minute, second);
}

static inline uint16_t vfs_fat_date(int year, int month, int day) {
    if (year < 1980) year = 1980;
    if (year > 2107) year = 2107;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    return (uint16_t)(((year - 1980) << 9) | (month << 5) | day);
}

static inline uint16_t vfs_fat_time(int hour, int minute, int second) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;
    if (second < 0) second = 0;
    if (second > 59) second = 59;
    return (uint16_t)((hour << 11) | (minute << 5) | (second / 2));
}

#endif
