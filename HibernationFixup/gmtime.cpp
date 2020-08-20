//
//  gmtime.cpp
//  HibernationFixup
//
//  Copyright © 2019 lvs1974. All rights reserved.
//

#include <stdint.h>
#include <sys/time.h>
#include <stddef.h>

#include "gmtime.h"

bool isLeapYear(int year)
{
	return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInYear(int year)
{
	return isLeapYear(year) ? 366 : 365;
}

int daysInMonth(int month, int year)
{
	static int days[] = {-1, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if ((month == 2) && (daysInYear(year) == 366)) return 29;
	else return days[month];
}

int reduceDaysToYear(time_t &days) {
	int year;
	for (year = 1970; days > daysInYear(year); year++) {
		days -= daysInYear(year);
	}
	return year;
}

int reduceDaysToMonths(time_t &days, int year) {
	int month;
	for (month = 0; days > daysInMonth(month,year); month++)
		days -= daysInMonth(month,year);
	return month;
}

struct tm * gmtime_r(time_t timer, struct tm * timeptr)
{
	timeptr->tm_sec = timer % 60;
	timer /= 60;
	timeptr->tm_min = timer % 60;
	timer /= 60;
	timeptr->tm_hour = timer % 24;
	timer /= 24;
	
	timeptr->tm_year  = reduceDaysToYear(timer);
	timeptr->tm_mon   = reduceDaysToMonths(timer, timeptr->tm_year);
	timeptr->tm_mday  = int(timer);
	
	return (timeptr);
}
