#ifndef OSCSERVER_H
#define OSCSERVER_H

#include <Arduino.h>
#include <WiFiUdp.h>

#define OSC_DEFAULT_PORT 8000
#define OSC_MAX_ARGS 8
#define OSC_MAX_BUNDLE_DEPTH 4
#define OSC_TX_BUFFER_SIZE 512

// Minimal OSC 1.0 receiver: messages + bundles (timetags ignored),
// argument types i, f, s, T, F. String args point into the RX buffer
// and are only valid for the duration of the handler call.

enum OSCArgType : uint8_t { OSC_INT, OSC_FLOAT, OSC_STRING, OSC_BOOL };

typedef struct {
  OSCArgType type;
  union {
    int32_t i;
    float f;
    const char* s;
    bool b;
  };
} OSCArg;

class OSCMessage {
  public:
    const char* address = nullptr;
    uint8_t argc = 0;
    OSCArg args[OSC_MAX_ARGS];

    bool argAsInt(uint8_t idx, int32_t& out) const;
    bool argAsFloat(uint8_t idx, float& out) const;
    bool argAsString(uint8_t idx, const char*& out) const;
    // True when there are no args or the first arg is non-zero/true.
    // Lets momentary buttons (1 on press, 0 on release) trigger once.
    bool isTrigger() const;
};

class OSCServer;
typedef void (*OSCHandler)(OSCServer& server, const OSCMessage& msg);

class OSCServer {
  public:
    void begin(uint16_t port);
    void stop();
    void setHandler(OSCHandler handler);
    void loop();
    uint16_t getPort() { return port; }

    // Reply to the sender of the packet currently being handled
    void beginReply(const char* address);
    void addInt(int32_t v);
    void addFloat(float v);
    void addString(const char* s);
    void sendReply();

  private:
    void parsePacket(const uint8_t* data, int len, uint8_t depth);
    bool parseMessage(const uint8_t* data, int len, OSCMessage& msg);

    WiFiUDP udp;
    uint8_t udpOpen = 0;
    uint16_t port = OSC_DEFAULT_PORT;
    OSCHandler handler = nullptr;
    uint8_t netRxBuffer[1500];

    uint8_t txArgs[OSC_TX_BUFFER_SIZE];
    int txArgsLen = 0;
    char txTags[OSC_MAX_ARGS + 2];
    uint8_t txTagCount = 0;
    char txAddress[64];
    bool txOverflow = false;
};

#endif /* OSCSERVER_H */
