// Atrapa DNSServer (Arduino-ESP32) pro kontrolu syntaxe – jen rozhraní, které firmware používá.
#pragma once
#include <stdint.h>
#include "IPAddress.h"
#include "WString.h"
enum class DNSReplyCode : uint8_t { NoError = 0, ServerFailure = 2, NonExistentDomain = 3 };
class DNSServer {
 public:
  bool start(uint16_t port, const String& domainName, const IPAddress& resolvedIP) { return true; }
  void stop() {}
  void processNextRequest() {}
  void setErrorReplyCode(const DNSReplyCode& replyCode) {}
  void setTTL(const uint32_t& ttl) {}
};
