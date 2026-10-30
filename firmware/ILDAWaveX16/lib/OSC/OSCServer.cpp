#include "OSCServer.h"

static inline int pad4(int n) { return (n + 3) & ~3; }

static inline uint32_t readBE32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void writeBE32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

// Returns the padded length of a null-terminated OSC string at data[0..len), or -1 if malformed
static int readPaddedString(const uint8_t* data, int len) {
  int n = 0;
  while (n < len && data[n] != '\0') n++;
  if (n >= len) return -1;
  int padded = pad4(n + 1);
  return padded <= len ? padded : -1;
}

// ---------------- OSCMessage ----------------

bool OSCMessage::argAsInt(uint8_t idx, int32_t& out) const {
  if (idx >= argc) return false;
  switch (args[idx].type) {
    case OSC_INT: out = args[idx].i; return true;
    case OSC_FLOAT: out = (int32_t)lroundf(args[idx].f); return true;
    case OSC_BOOL: out = args[idx].b ? 1 : 0; return true;
    default: return false;
  }
}

bool OSCMessage::argAsFloat(uint8_t idx, float& out) const {
  if (idx >= argc) return false;
  switch (args[idx].type) {
    case OSC_INT: out = (float)args[idx].i; return true;
    case OSC_FLOAT: out = args[idx].f; return true;
    case OSC_BOOL: out = args[idx].b ? 1.0f : 0.0f; return true;
    default: return false;
  }
}

bool OSCMessage::argAsString(uint8_t idx, const char*& out) const {
  if (idx >= argc || args[idx].type != OSC_STRING) return false;
  out = args[idx].s;
  return true;
}

bool OSCMessage::isTrigger() const {
  if (argc == 0) return true;
  float v;
  if (argAsFloat(0, v)) return v != 0.0f;
  return true;
}

// ---------------- OSCServer ----------------

void OSCServer::begin(uint16_t p) {
  port = p;
  udp.begin(port);
  Serial.printf("OSC UDP Port: %i\n", port);
  udpOpen = 1;
}

void OSCServer::stop() {
  udp.stop();
  udpOpen = 0;
}

void OSCServer::setHandler(OSCHandler h) { handler = h; }

void OSCServer::loop() {
  if (udpOpen == 0 || udp.parsePacket() == 0) return;

  int len = udp.read(netRxBuffer, sizeof(netRxBuffer));
  if (len <= 0) return;

  parsePacket(netRxBuffer, len, 0);
}

void OSCServer::parsePacket(const uint8_t* data, int len, uint8_t depth) {
  if (len < 4 || (len & 3) != 0) return;

  if (data[0] == '/') {
    OSCMessage msg;
    if (parseMessage(data, len, msg) && handler) handler(*this, msg);
    return;
  }

  // Bundle: "#bundle\0" + 8 byte timetag + [int32 size + element]*
  if (len < 16 || memcmp(data, "#bundle", 8) != 0) return;
  if (depth >= OSC_MAX_BUNDLE_DEPTH) return;

  int offset = 16;
  while (offset + 4 <= len) {
    int32_t size = (int32_t)readBE32(data + offset);
    offset += 4;
    if (size <= 0 || offset + size > len) return;
    parsePacket(data + offset, size, depth + 1);
    offset += size;
  }
}

bool OSCServer::parseMessage(const uint8_t* data, int len, OSCMessage& msg) {
  int addrLen = readPaddedString(data, len);
  if (addrLen < 0) return false;
  msg.address = (const char*)data;
  msg.argc = 0;

  int offset = addrLen;
  if (offset >= len) return true; // No type tag string - treat as no args

  if (data[offset] != ',') return false;
  int tagsLen = readPaddedString(data + offset, len - offset);
  if (tagsLen < 0) return false;
  const char* tags = (const char*)data + offset + 1;
  offset += tagsLen;

  for (const char* t = tags; *t != '\0'; t++) {
    if (msg.argc >= OSC_MAX_ARGS) break;
    OSCArg& arg = msg.args[msg.argc];

    switch (*t) {
      case 'i': {
        if (offset + 4 > len) return false;
        arg.type = OSC_INT;
        arg.i = (int32_t)readBE32(data + offset);
        offset += 4;
        break;
      }
      case 'f': {
        if (offset + 4 > len) return false;
        uint32_t raw = readBE32(data + offset);
        arg.type = OSC_FLOAT;
        memcpy(&arg.f, &raw, sizeof(float));
        offset += 4;
        break;
      }
      case 's': {
        int sLen = readPaddedString(data + offset, len - offset);
        if (sLen < 0) return false;
        arg.type = OSC_STRING;
        arg.s = (const char*)data + offset;
        offset += sLen;
        break;
      }
      case 'T':
      case 'F':
        arg.type = OSC_BOOL;
        arg.b = (*t == 'T');
        break;
      default:
        // Unsupported type - can't know its size, so stop reading args
        return true;
    }
    msg.argc++;
  }
  return true;
}

void OSCServer::beginReply(const char* address) {
  strncpy(txAddress, address, sizeof(txAddress) - 1);
  txAddress[sizeof(txAddress) - 1] = '\0';
  txTags[0] = ',';
  txTagCount = 0;
  txArgsLen = 0;
  txOverflow = false;
}

void OSCServer::addInt(int32_t v) {
  if (txTagCount >= OSC_MAX_ARGS || txArgsLen + 4 > OSC_TX_BUFFER_SIZE) { txOverflow = true; return; }
  writeBE32(txArgs + txArgsLen, (uint32_t)v);
  txArgsLen += 4;
  txTags[1 + txTagCount++] = 'i';
}

void OSCServer::addFloat(float v) {
  if (txTagCount >= OSC_MAX_ARGS || txArgsLen + 4 > OSC_TX_BUFFER_SIZE) { txOverflow = true; return; }
  uint32_t raw;
  memcpy(&raw, &v, sizeof(float));
  writeBE32(txArgs + txArgsLen, raw);
  txArgsLen += 4;
  txTags[1 + txTagCount++] = 'f';
}

void OSCServer::addString(const char* s) {
  int n = strlen(s);
  int padded = pad4(n + 1);
  if (txTagCount >= OSC_MAX_ARGS || txArgsLen + padded > OSC_TX_BUFFER_SIZE) { txOverflow = true; return; }
  memcpy(txArgs + txArgsLen, s, n);
  memset(txArgs + txArgsLen + n, 0, padded - n);
  txArgsLen += padded;
  txTags[1 + txTagCount++] = 's';
}

void OSCServer::sendReply() {
  if (txOverflow) { Serial.println("OSC reply too large - dropped"); return; }

  static const uint8_t zeros[4] = { 0, 0, 0, 0 };
  int addrLen = strlen(txAddress);
  int tagsLen = 1 + txTagCount;

  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write((const uint8_t*)txAddress, addrLen);
  udp.write(zeros, pad4(addrLen + 1) - addrLen);
  udp.write((const uint8_t*)txTags, tagsLen);
  udp.write(zeros, pad4(tagsLen + 1) - tagsLen);
  udp.write(txArgs, txArgsLen);
  udp.endPacket();
}
