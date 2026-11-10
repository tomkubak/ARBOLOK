// ============================================================================
//  jsonbuf.h  –  minimální zapisovač JSONu do pevného bufferu (ČISTÉ C++)
//
//  Bez dynamické paměti a bez ArduinoJson (šetří flash i heap). Komu se
//  buffer zaplní, ten se označí jako "přetečený" (ok() == false) a už se do něj
//  nezapisuje – výsledek je pak vždy platný řetězec, jen nekompletní, takže
//  volající musí zkontrolovat ok().
// ============================================================================
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class JsonBuf {
 public:
  JsonBuf(char* buf, size_t cap) : b_(buf), cap_(cap), n_(0), ok_(cap > 1), depth_(-1), afterKey_(false) {
    if (cap_) b_[0] = '\0';
    for (int i = 0; i < kMaxDepth; i++) comma_[i] = false;
  }

  bool ok() const { return ok_; }
  size_t size() const { return n_; }
  const char* c_str() const { return b_; }

  JsonBuf& obj() { return open('{'); }
  JsonBuf& arr() { return open('['); }
  JsonBuf& end() {
    if (depth_ < 0) {
      ok_ = false;
      return *this;
    }
    const char c = closer_[depth_];
    depth_--;
    afterKey_ = false;
    put(c);
    return *this;
  }

  JsonBuf& key(const char* k) {
    pre();
    quoted(k);
    put(':');
    afterKey_ = true;
    return *this;
  }

  JsonBuf& str(const char* s) {
    pre();
    quoted(s ? s : "");
    return *this;
  }
  JsonBuf& num(double v, int decimals = 3) {
    pre();
    if (!isfinite(v)) {
      lit("null");
      return *this;
    }
    char t[40];
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    const int k = snprintf(t, sizeof(t), "%.*f", decimals, v);
    if (k < 0 || (size_t)k >= sizeof(t)) {
      ok_ = false;
      return *this;
    }
    lit(t);
    return *this;
  }
  JsonBuf& integer(long v) {
    pre();
    char t[24];
    snprintf(t, sizeof(t), "%ld", v);
    lit(t);
    return *this;
  }
  JsonBuf& uinteger(unsigned long v) {
    pre();
    char t[24];
    snprintf(t, sizeof(t), "%lu", v);
    lit(t);
    return *this;
  }
  JsonBuf& boolean(bool v) {
    pre();
    lit(v ? "true" : "false");
    return *this;
  }
  JsonBuf& null() {
    pre();
    lit("null");
    return *this;
  }

  // zkratky "klíč: hodnota"
  JsonBuf& kvStr(const char* k, const char* v) { return key(k).str(v); }
  JsonBuf& kvNum(const char* k, double v, int dec = 3) { return key(k).num(v, dec); }
  JsonBuf& kvInt(const char* k, long v) { return key(k).integer(v); }
  JsonBuf& kvUInt(const char* k, unsigned long v) { return key(k).uinteger(v); }
  JsonBuf& kvBool(const char* k, bool v) { return key(k).boolean(v); }

 private:
  static const int kMaxDepth = 8;

  JsonBuf& open(char c) {
    pre();
    if (depth_ + 1 >= kMaxDepth) {
      ok_ = false;
      return *this;
    }
    put(c);
    depth_++;
    comma_[depth_] = false;
    closer_[depth_] = (c == '{') ? '}' : ']';
    return *this;
  }

  // před hodnotou / klíčem: případná čárka
  void pre() {
    if (afterKey_) {
      afterKey_ = false;
      return;
    }
    if (depth_ >= 0) {
      if (comma_[depth_]) put(',');
      comma_[depth_] = true;
    }
  }

  void put(char c) {
    if (!ok_) return;
    if (n_ + 1 >= cap_) {
      ok_ = false;
      return;
    }
    b_[n_++] = c;
    b_[n_] = '\0';
  }

  void lit(const char* s) {
    for (; *s; ++s) put(*s);
  }

  void quoted(const char* s) {
    put('"');
    for (; *s; ++s) {
      const unsigned char c = (unsigned char)*s;
      switch (c) {
        case '"': put('\\'); put('"'); break;
        case '\\': put('\\'); put('\\'); break;
        case '\n': put('\\'); put('n'); break;
        case '\r': put('\\'); put('r'); break;
        case '\t': put('\\'); put('t'); break;
        default:
          if (c < 0x20) {
            char t[8];
            snprintf(t, sizeof(t), "\\u%04x", (unsigned)c);
            lit(t);
          } else {
            put((char)c);  // UTF-8 bajty procházejí beze změny
          }
      }
    }
    put('"');
  }

  char* b_;
  size_t cap_, n_;
  bool ok_;
  int depth_;
  bool afterKey_;
  bool comma_[kMaxDepth];
  char closer_[kMaxDepth];
};
