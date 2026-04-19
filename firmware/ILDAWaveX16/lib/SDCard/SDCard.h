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
  private:
    String cachedFileRows = "";
    unsigned long lastCacheUpdate = 0;
    static const unsigned long CACHE_DURATION_MS = 30000; // 30 seconds
};

#endif /* SDCARD_H */