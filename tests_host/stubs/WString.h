#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))

class String {
 public:
  String(const char* cstr = "") : s_(cstr ? cstr : "") {}
  String(const char* cstr, unsigned int length) : s_(cstr ? std::string(cstr, length) : std::string()) {}
  String(const String& o) = default;
  String(String&& o) = default;
  String(const __FlashStringHelper* p) : s_(reinterpret_cast<const char*>(p)) {}
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char v, unsigned char base = 10) : s_(std::to_string((unsigned)v)) { (void)base; }
  explicit String(int v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(unsigned int v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(long v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(unsigned long v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(long long v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(unsigned long long v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(float v, unsigned char dec = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", dec, (double)v); s_ = b; }
  explicit String(double v, unsigned char dec = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", dec, v); s_ = b; }
  String& operator=(const String&) = default;
  String& operator=(String&&) = default;
  String& operator=(const char* c) { s_ = c ? c : ""; return *this; }

  bool reserve(unsigned int n) { s_.reserve(n); return true; }
  unsigned int length() const { return (unsigned int)s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  bool isEmpty() const { return s_.empty(); }
  void clear() { s_.clear(); }

  bool concat(const String& s) { s_ += s.s_; return true; }
  bool concat(const char* s) { s_ += s ? s : ""; return true; }
  bool concat(char c) { s_ += c; return true; }
  bool concat(const char* s, unsigned int n) { s_.append(s, n); return true; }
  bool concat(unsigned char v) { s_ += std::to_string((unsigned)v); return true; }
  bool concat(int v) { s_ += std::to_string(v); return true; }
  bool concat(unsigned int v) { s_ += std::to_string(v); return true; }
  bool concat(long v) { s_ += std::to_string(v); return true; }
  bool concat(unsigned long v) { s_ += std::to_string(v); return true; }
  bool concat(float v) { s_ += std::to_string(v); return true; }
  bool concat(double v) { s_ += std::to_string(v); return true; }
  bool concat(const __FlashStringHelper* p) { s_ += reinterpret_cast<const char*>(p); return true; }
  String& operator+=(const String& r) { concat(r); return *this; }
  String& operator+=(const char* r) { concat(r); return *this; }
  String& operator+=(char r) { concat(r); return *this; }
  String& operator+=(unsigned char r) { concat(r); return *this; }
  String& operator+=(int r) { concat(r); return *this; }
  String& operator+=(unsigned int r) { concat(r); return *this; }
  String& operator+=(long r) { concat(r); return *this; }
  String& operator+=(unsigned long r) { concat(r); return *this; }
  String& operator+=(const __FlashStringHelper* r) { concat(r); return *this; }

  int compareTo(const String& s) const { return s_.compare(s.s_); }
  bool equals(const String& s) const { return s_ == s.s_; }
  bool equals(const char* c) const { return s_ == (c ? c : ""); }
  bool equalsIgnoreCase(const String& s) const { return strcasecmp(s_.c_str(), s.s_.c_str()) == 0; }
  bool operator==(const String& r) const { return equals(r); }
  bool operator==(const char* r) const { return equals(r); }
  bool operator!=(const String& r) const { return !equals(r); }
  bool operator!=(const char* r) const { return !equals(r); }
  bool operator<(const String& r) const { return compareTo(r) < 0; }
  bool operator>(const String& r) const { return compareTo(r) > 0; }
  bool operator<=(const String& r) const { return compareTo(r) <= 0; }
  bool operator>=(const String& r) const { return compareTo(r) >= 0; }

  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }
  char& operator[](unsigned int i) { return s_[i]; }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  bool startsWith(const String& p, unsigned int off) const { return s_.compare(off, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String& p) const { return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0; }
  int indexOf(char c, unsigned int from = 0) const { auto p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& s, unsigned int from = 0) const { auto p = s_.find(s.s_, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(const String& s) const { auto p = s_.rfind(s.s_); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from).c_str()); }
  String substring(unsigned int from, unsigned int to) const {
    if (to < from) { unsigned t = to; to = from; from = t; }
    if (from >= s_.size()) return String();
    return String(s_.substr(from, to - from).c_str());
  }
  void replace(char a, char b) { for (auto& c : s_) if (c == a) c = b; }
  void replace(const String& a, const String& b) {
    if (a.s_.empty()) return;
    size_t p = 0;
    while ((p = s_.find(a.s_, p)) != std::string::npos) { s_.replace(p, a.s_.size(), b.s_); p += b.s_.size(); }
  }
  void remove(unsigned int idx) { if (idx < s_.size()) s_.erase(idx); }
  void remove(unsigned int idx, unsigned int count) { if (idx < s_.size()) s_.erase(idx, count); }
  void toLowerCase() { for (auto& c : s_) c = (char)tolower(c); }
  void toUpperCase() { for (auto& c : s_) c = (char)toupper(c); }
  void trim() {
    size_t b = s_.find_first_not_of(" \t\r\n"), e = s_.find_last_not_of(" \t\r\n");
    s_ = b == std::string::npos ? "" : s_.substr(b, e - b + 1);
  }
  long toInt() const { return atol(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }
  double toDouble() const { return atof(s_.c_str()); }
  explicit operator bool() const { return !s_.empty(); }

  friend String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
  friend String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, char b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, int b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, unsigned int b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, long b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, unsigned long b) { String r(a); r += b; return r; }
  friend String operator+(const String& a, const __FlashStringHelper* b) { String r(a); r += b; return r; }

 private:
  std::string s_;
};

extern const String emptyString;
