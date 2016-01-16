#include "string.h"

#include <algorithm>
#include <cstdlib>

#include <err.h>
#include <stdarg.h>
#include <sysexits.h>

#include <kj/debug.h>

namespace ev {

std::string StringPrintf(const char* format, ...) {
  va_list args;
  char* buf;

  va_start(args, format);

  KJ_SYSCALL(vasprintf(&buf, format, args));

  std::string result(buf);
  free(buf);

  return result;
}

std::vector<ev::StringRef> Explode(const ev::StringRef& string,
                                   const ev::StringRef& delimiter,
                                   size_t limit) {
  std::vector<ev::StringRef> result;

  for (auto i = string.begin(); i != string.end();) {
    if (limit && result.size() + 1 == limit) {
      result.emplace_back(i, string.end());
      break;
    }

    auto d = std::search(i, string.end(), delimiter.begin(), delimiter.end());

    result.emplace_back(i, d);

    i = d;

    if (i != string.end()) i += delimiter.end() - delimiter.begin();
  }

  return result;
}

std::string Trim(std::string str) {
  auto begin = str.begin();
  while (begin != str.end() && std::isspace(*begin))
    ++begin;
  str.erase(str.begin(), begin);

  while (!str.empty() && std::isspace(str.back()))
    str.pop_back();

  return str;
}

std::string ToLower(std::string str) {
  for (auto& ch : str) ch = std::tolower(ch);
  return str;
}

int64_t StringToInt64(const char* string) {
  static_assert(std::is_same<long, int64_t>::value,
                "long is not equal to int64_t");
  KJ_REQUIRE(*string != 0);
  char* endptr = nullptr;
  errno = 0;
  const auto value = strtol(string, &endptr, 0);
  KJ_REQUIRE(*endptr == 0, "unexpected character in numeric string", string);
  if (errno != 0) {
    KJ_FAIL_SYSCALL("strtol", errno, string);
  }
  return value;
}

uint64_t StringToUInt64(ev::StringRef string) {
  KJ_REQUIRE(!string.empty());
  KJ_CONTEXT(string);

  uint64_t last_value = 0, value = 0;
  if (ev::HasPrefix(string, "0x")) {
    static const unsigned char kHexHelper[26] = {
        0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0,
        0, 0,  0,  0,  1,  2,  3,  4, 5, 6, 7, 8, 9};
    string.Consume(2);

    for (const auto ch : string) {
      KJ_REQUIRE(std::isxdigit(ch), ch);
      value = value * 16 + kHexHelper[ch & 0x1f];
      KJ_REQUIRE(value >= last_value, "overflow");
      last_value = value;
    }
  } else if (ev::HasPrefix(string, "0")) {
    string.Consume(1);
    for (const auto ch : string) {
      int digit = (ch - '0');
      KJ_REQUIRE(digit >= 0 && digit <= 7, ch);
      value = value * 8 + digit;
      KJ_REQUIRE(value >= last_value, "overflow");
      last_value = value;
    }
  } else {
    for (const auto ch : string) {
      int digit = (ch - '0');
      KJ_REQUIRE(digit >= 0 && digit <= 9, ch);
      value = value * 10 + digit;
      KJ_REQUIRE(value >= last_value, "overflow");
      last_value = value;
    }
  }

  return value;
}

double StringToDouble(const char* string) {
  char* endptr = nullptr;
  auto value = strtod(string, &endptr);
  KJ_REQUIRE(*endptr == 0, "unexpected character in numeric string", string);
  return value;
}

float StringToFloat(const char* string) {
  char* endptr = nullptr;
  auto value = strtof(string, &endptr);
  KJ_REQUIRE(*endptr == 0, "unexpected character in numeric string", string);
  return value;
}

std::string DoubleToString(const double v) {
  if (!v) return "0";

  if ((v >= 1e-6 || v <= -1e-6) && v < 1e17 && v > -1e17) {
    for (int prec = 0; prec < 17; ++prec) {
      auto result = StringPrintf("%.*f", prec, v);
      auto test_v = strtod(result.c_str(), nullptr);
      if (test_v == v) return result;
    }
  }

  return StringPrintf("%.17g", v);
}

std::string FloatToString(const float v) {
  if (!v) return "0";

  if ((v >= 1e-6 || v <= -1e-6) && v < 1e9 && v > -1e9) {
    for (int prec = 0; prec < 9; ++prec) {
      auto result = StringPrintf("%.*f", prec, v);
      auto test_v = strtof(result.c_str(), nullptr);
      if (test_v == v) return result;
    }
  }

  return StringPrintf("%.9g", v);
}

}  // namespace ev
