
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * version 1: 05/2012 put together to get a basic rtc clock driver by Rafirafi
 * vers. 1.1: 08/2103 clean-up
 *
 * Part of it originally from linux kernel
 */


#include <IOKit/IOLib.h>
#include <architecture/i386/pio.h>

#include "ClockRTC.h"
#include "ClockRTC_regs.h"

#define RTCCLOCK_DEBUG

#ifdef RTCCLOCK_DEBUG
#define DLOG(fmt, args...)  IOLog(fmt, ## args)
#else
#define DLOG(fmt, args...)
#endif

inline unsigned bcd2binL(unsigned char val)
{
    return (val & 0x0f) + (val >> 4) * 10;
}

inline unsigned char bin2bcdL(unsigned val)
{
    return ((val / 10) << 4) + val % 10;
}

OSDefineMetaClassAndStructors(ClockRTC, IOService);

#undef super
#define super IOService

bool ClockRTC::init(OSDictionary * dict)
{
    if (!super::init(dict)) {
        return false;
    }

    /* need to protect in case interrupt being accessed */
    rtcSimpleLock = IOSimpleLockAlloc();

    if (!rtcSimpleLock) {
        return false;
    }

    return true;
}

void ClockRTC::free(void)
{
    if (rtcSimpleLock) {
        IOSimpleLockFree(rtcSimpleLock);
        rtcSimpleLock = NULL;
    }

    super::free();
}

bool ClockRTC::start(IOService * provider)
{
    DLOG("%s::%s\n",  getName(), __func__);

    if (!super::start(provider)) {
        return false;
    }

    registerService();

    publishResource("IORTC", this);

    return true;
}

void ClockRTC::stop(IOService * provider)
{
    DLOG("%s::%s\n",  getName(), __func__);

    super::stop(provider);
}

/* basic brutal read */
UInt8 ClockRTC::rtcRead(IOByteCount reg)
{
    unsigned char val;
    outb(RTC_PORT(0),reg);
    val = inb(RTC_PORT(1));
    return val;
}

/* basic brutal write */
void ClockRTC::rtcWrite(IOByteCount reg, UInt8 data)
{
    outb(RTC_PORT(0), reg);
    outb(RTC_PORT(1), data);
}

long ClockRTC::getGMTTimeOfDay(void)
{
    long date;
    date = mach_get_cmos_time();

#ifdef RTCCLOCK_DEBUG
    RTCCalendar_t tm;
    to_tm(date, &tm);
    DLOG("%s::%s %d-%d-%d %d:%d:%d\n", getName(), __func__,
         (int)tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);
#endif

    return date;
}

void ClockRTC::setGMTTimeOfDay(long date)
{
    RTCCalendar_t tm;

    /* convert date to rtc date */
    to_tm(date, &tm);

    DLOG("%s::%s %d-%d-%d %d:%d:%d\n", getName(), __func__,
         (int)tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);

    mach_set_cmos_time(&tm);
}

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines where long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
unsigned long ClockRTC::mktime(const unsigned int year0, const unsigned int mon0,
                               const unsigned int day, const unsigned int hour,
                               const unsigned int mins, const unsigned int sec)
{
    unsigned int mon = mon0, year = year0;

    /* 1..12 -> 11,12,1..10 */
    if (0 >= (int)(mon -= 2)) {
        mon += 12; /* Puts Feb last since it has leap day */
        year -= 1;
    }

    return ((((unsigned long)
              (year / 4 - year / 100 + year / 400 + 367 * mon / 12 + day) +
              year * 365 - 719499
              ) * 24 + hour /* now have hours */
             ) * 60 + mins /* now have minutes */
            ) * 60 + sec; /* finally seconds */
}

unsigned long ClockRTC::mach_get_cmos_time(void)
{
    unsigned int status, year, mon, day, hour, mins, sec = 0;

    IOInterruptState intrState;

    intrState = IOSimpleLockLockDisableInterrupt(rtcSimpleLock);

    /*
     * If UIP is clear, then we have >= 244 microseconds before
     * RTC registers will be updated.  Spec sheet says that this
     * is the reliable way to read RTC - registers. If UIP is set
     * then the register access might be invalid.
     */
    while ((rtcRead(RTC_REG_A) & RTC_UIP)) {
        ; /* cpu_relax */
    }

    sec = rtcRead(RTC_SECONDS);
    mins = rtcRead(RTC_MINUTES);
    hour = rtcRead(RTC_HOURS);
    day = rtcRead(RTC_DAY_OF_MONTH);
    mon = rtcRead(RTC_MONTH);
    year = rtcRead(RTC_YEAR);

    status = rtcRead(RTC_REG_B);

    IOSimpleLockUnlockEnableInterrupt(rtcSimpleLock, intrState);

    if (!(status & RTC_DM_BINARY)) {
        sec = bcd2binL(sec);
        mins = bcd2binL(mins);
        hour = bcd2binL(hour);
        day = bcd2binL(day);
        mon = bcd2binL(mon);
        year = bcd2binL(year);
    }

    /* use harcoded value */
    year += CMOS_YEARS_OFFS;

    return mktime(year, mon, day, hour, mins, sec);
}

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
void ClockRTC::GregorianDay(RTCCalendar_t *tm)
{
    int leapsToDate;
    int lastYear;
    int day;
    int MonthOffset[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

    lastYear = tm->year - 1;

    /* Number of leap corrections to apply up to end of last year */
    leapsToDate = lastYear / 4 - lastYear / 100 + lastYear / 400;

    /*
     * This year is a leap year if it is divisible by 4 except when it is
     * divisible by 100 unless it is divisible by 400
     *
     * e.g. 1904 was a leap year, 1900 was not, 1996 is, and 2000 was
     */
    day = tm->month > 2 && leapyear(tm->year);

    day += lastYear * 365 + leapsToDate + MonthOffset[tm->month - 1] + tm->day;

    tm->dayOfWeek = day % 7;
}

void ClockRTC::to_tm(int tim, RTCCalendar_t *tm)
{
    register int    i;
    register long   hms, day;

    day = tim / SECDAY;
    hms = tim % SECDAY;

    /* Hours, minutes, seconds are easy */
    tm->hour = hms / 3600;
    tm->minute = (hms % 3600) / 60;
    tm->second = (hms % 3600) % 60;

    /* Number of years in days */
    for (i = STARTOFTIME; day >= days_in_year(i); i++) {
        day -= days_in_year(i);
    }
    tm->year = i;

    /* Number of months in days left */
    if (leapyear(tm->year)) {
        days_in_month(FEBRUARY) = 29;
    }
    for (i = 1; day >= days_in_month(i); i++) {
        day -= days_in_month(i);
    }
    days_in_month(FEBRUARY) = 28;
    tm->month = i;

    /* Days are what is left over (+1) from all that. */
    tm->day = day + 1;

    /* Determine the day of week */
    GregorianDay(tm);
}

void ClockRTC::mach_set_cmos_time(RTCCalendar_t *tm)
{
    IOInterruptState intrState;
    UInt8 mask = 0;
    UInt8 save_control;

    /* get value from tm in register order */
    UInt8 second = tm->second;
    UInt8 minute = tm->minute;
    UInt8 hour = tm->hour;
    UInt8 day = tm->day;
    UInt8 dayOfWeek = tm->dayOfWeek;
    UInt8 month = tm->month;
    UInt8 year = tm->year - CMOS_YEARS_OFFS;

    /*DLOG("%s : 00 : year:%d year.ORI:%d month:%d day:%d DayOfWeek:%d hour:%d minute:%d second:%d\n",
         __func__, (int)year, month, day, dayOfWeek, hour, minute, second);*/

    intrState = IOSimpleLockLockDisableInterrupt(rtcSimpleLock);
    save_control = rtcRead(RTC_CONTROL);
    IOSimpleLockUnlockEnableInterrupt(rtcSimpleLock, intrState);

    /* take care of hour cmos spec for AM/PM */
    if (hour > 12 && !(save_control & RTC_24H)) {
        hour = hour - 12;
        mask = 0x80;
    }

    /* convert to bcd if necessary */
    if (!(save_control & RTC_DM_BINARY)) {
        second = bin2bcdL(second);
        minute = bin2bcdL(minute);
        hour = bin2bcdL(hour);
        day = bin2bcdL(day);
        dayOfWeek = bin2bcdL(dayOfWeek);
        month = bin2bcdL(month);
        year = bin2bcdL(year);
    }

    /*DLOG("%s : 0d : year:%d month:%d day:%d DayOfWeek:%d hour:%d minute:%d second:%d\n",
         __func__, (int)year, month, day, dayOfWeek, hour, minute, second);*/

    /*DLOG("%s : 0x : year:%x month:%x day:%x DayOfWeek:%x hour:%x minute:%x second:%x\n",
         __func__, (int)year, month, day, dayOfWeek, hour, minute, second);*/

    /* Apply mask */
    hour = hour | mask;

    intrState = IOSimpleLockLockDisableInterrupt(rtcSimpleLock);

    save_control = rtcRead(RTC_CONTROL);
    rtcWrite(RTC_CONTROL, (save_control|RTC_SET));

    rtcWrite(RTC_SECONDS, second);
    rtcWrite(RTC_MINUTES, minute);
    rtcWrite(RTC_HOURS, hour);
    rtcWrite(RTC_DAY_OF_MONTH, day);
    rtcWrite(RTC_DAY_OF_WEEK, dayOfWeek);
    rtcWrite(RTC_MONTH, month);
    rtcWrite(RTC_YEAR, year);

    rtcWrite(RTC_CONTROL, save_control);

    IOSimpleLockUnlockEnableInterrupt(rtcSimpleLock, intrState);
}
