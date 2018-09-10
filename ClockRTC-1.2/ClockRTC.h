
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
 * methods : mktime, GregorianDay, to_tm, mach_get_cmos_time
 * originally from linux kernel
 */

#ifndef _CLOCKRTC_H
#define _CLOCKRTC_H

#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>

/* IOPMCalendarStruct + dayOfWeek put in the register order */
typedef struct {
    UInt8       second;
    UInt8       minute;
    UInt8       hour;
    UInt8       day;
    UInt8		dayOfWeek;
    UInt8       month;
    UInt32      year;
} RTCCalendar_t;

class ClockRTC : public IOService
{
    OSDeclareDefaultStructors(ClockRTC);

private:
    IOSimpleLock * rtcSimpleLock; 
    UInt8   rtcRead(IOByteCount reg);
    void	rtcWrite(IOByteCount reg, UInt8 data);
    unsigned long mktime(const unsigned int year0, const unsigned int mon0,
                         const unsigned int day, const unsigned int hour,
                         const unsigned int mins, const unsigned int sec);
    void GregorianDay(RTCCalendar_t * tm);
    void to_tm(int tim, RTCCalendar_t * tm);
    unsigned long mach_get_cmos_time(void);
    void mach_set_cmos_time(RTCCalendar_t *tm);

public:
    bool init(OSDictionary *dict);
    void free(void);
    virtual bool start(IOService * provider);
    virtual void stop(IOService * provider);
    /* for I386GenericPlatform */
    virtual	long	getGMTTimeOfDay(void);
    virtual	void	setGMTTimeOfDay(long date);
};

#endif /* _CLOCKRTC_H */
