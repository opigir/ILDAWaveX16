#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <vector>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

using namespace std;

#define PIN_SD_CS 16
#define PIN_SD_MISO 13
#define PIN_SD_MOSI 15
#define PIN_SD_SCK 14

class SDCard {
  public:
    void begin();
    void mount();
    void list();
    void listFiles(vector<String>& list, const char* path = "/");
    String generateFileRows();
    void refreshFileCache();
    void read(const char* path);
    File getFile(const char* path);
    bool fileExists(const char* path);
    bool isAvailable();
    bool checkAndMount();
  private:
    String cachedFileRows = "";
    unsigned long lastCacheUpdate = 0;
    static const unsigned long CACHE_DURATION_MS = 30000; // 30 seconds
    static const unsigned long CACHE_DURATION_UNAVAILABLE_MS = 1000; // 1 second when SD unavailable
    bool sdCardMounted = false;
    unsigned long lastMountAttempt = 0;
    static const unsigned long MOUNT_COOLDOWN_MS = 2000; // Wait 2 seconds between mount attempts
};

#endif /* SDCARD_H */