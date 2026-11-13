#include "Lunar.h"
namespace jd {
namespace {
struct Year {
  uint8_t newYear, leapMonth;
  uint16_t longMonths;
};
constexpr Year years[] = {
#include "../generated/lunar_years.inc"
};
bool leap(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
}  // namespace
Str lunarDate(const struct tm& date) {
  int year = date.tm_year + 1900;
  if (year < 2020 || year > 2099) {
    return {};
  }
  auto info = years[year - 2019];
  int delta = date.tm_yday - info.newYear;
  if (delta < 0) {
    --year;
    info = years[year - 2019];
    delta = date.tm_yday + 365 + leap(year) - info.newYear;
  }
  static const char* months[] = {"正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "十一", "腊"};
  static const char* numbers[] = {"十", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
  for (int i = 0; i < 12 + bool(info.leapMonth); ++i) {
    int length = 29 + ((info.longMonths >> i) & 1);
    if (delta >= length) {
      delta -= length;
      continue;
    }
    bool extra = info.leapMonth && i == info.leapMonth;
    int month = i - (info.leapMonth && i >= info.leapMonth);
    int day = delta + 1;
    Str label = "农历 ";
    if (extra) {
      label += "闰";
    }
    label += months[month];
    label += "月";
    if (day == 20 || day == 30) {
      return label + (day == 20 ? "二十" : "三十");
    }
    label += day <= 10 ? "初" : day < 20 ? "十" : "廿";
    return label + numbers[day % 10];
  }
  return {};
}
}  // namespace jd
