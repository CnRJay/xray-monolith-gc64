#pragma once

#include <string>
#include <chrono>
#include <time.h>
#include <sstream>
#include <ctime>
#include <iomanip>

using xr_time_t = std::time_t;

template <typename TP>
xr_time_t xr_chrono_to_time_t(TP tp)
{
	using namespace std::chrono;
	auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());

	return system_clock::to_time_t(sctp);
}

xr_string getCurrentTimeStamp(const char* format = "%H:%M:%S")
{
	using namespace std::chrono;

	// get current time
	auto now = system_clock::now();

	// get number of milliseconds for the current second
	// (remainder after division into seconds)
	auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	// convert to std::time_t in order to convert to std::tm (broken time)
	xr_time_t timer = system_clock::to_time_t(now);

	// convert to broken time
	std::tm bt = *std::localtime(&timer);

	std::ostringstream oss;

	oss << std::put_time(&bt, format); // HH:MM:SS
	oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

	return oss.str().c_str();
}

xr_string timeInDMYHMSMMM()
{
	return getCurrentTimeStamp("%d.%m.%Y %H:%M:%S");
}
xr_string timeInHMSMMM()
{
	return getCurrentTimeStamp("%H:%M:%S");
}