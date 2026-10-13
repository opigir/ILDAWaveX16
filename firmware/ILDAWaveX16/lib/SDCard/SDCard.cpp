#include "SDCard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

SPIClass SDSPI(FSPI);

// Skip hidden and OS metadata folders (e.g. macOS .Spotlight-V100, .fseventsd, .Trashes) -
// they never hold show files, and walking .Spotlight-V100 alone can take 7+ seconds
static bool isSkippedDir(const char* name) {
  return name[0] == '.' || strcmp(name, "System Volume Information") == 0;
}

void SDCard::begin() {
  SDSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
}

void SDCard::mount() {
  // Single attempt to mount, no retries
  if (!SD.begin(PIN_SD_CS, SDSPI)) {
    Serial.println("Card Mount Failed");
    sdCardMounted = false;
    return;
  }
  sdCardMounted = true;
  Serial.println("SD card mounted successfully");
  return;
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  }
  else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));

  // listDir(SD, "/", 0);
  // readFile(SD, "/ilda.ild");
}

void SDCard::list() {
  const char* dirname = "/";

  Serial.printf("Listing directory: %s\n", dirname);

  File root = SD.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
    }
    else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void SDCard::listFiles(vector<String>& list, const char* path) {
  if (!sdCardMounted) {
    return;
  }

  File root = SD.open(path);
  if (!root) {
    Serial.println("Failed to open directory - skipping");
    return;
  }

  int fileCount = 0;
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      if (!isSkippedDir(file.name())) listFiles(list, file.path());
    } else if (strstr(file.path(), ".ild") || strstr(file.path(), ".ILD")) {
      // Skip any hidden files that start with dot
      if (file.name()[0] != '.') {
        list.push_back(file.path());
      }
    }
    file.close();
    file = root.openNextFile();

    // Yield control every 5 files to prevent watchdog timeout
    fileCount++;
    if (fileCount % 5 == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  root.close();
}

String listFilesRecursive(const char *dirname, int depth) {
  String rows;

  // Prevent infinite recursion and limit scan time
  if (depth > 5) {
    return rows;
  }

  File dir = SD.open(dirname);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return "";
  }

  int fileCount = 0;
  unsigned long startTime = millis();
  const unsigned long MAX_OPERATION_TIME = 20000; // 20 seconds max to prevent hanging

  while (true) {
    // Check for timeout to prevent watchdog issues
    if (millis() - startTime > MAX_OPERATION_TIME) {
      Serial.println("File listing timeout - stopping scan (too many files)");
      break;
    }

    File file = dir.openNextFile();
    if (!file) break;

    String fullPath = String(dirname);
    if (!fullPath.endsWith("/") && strlen(dirname) > 1) fullPath += "/";
    fullPath += file.name();

    if (file.isDirectory()) {
      if (!isSkippedDir(file.name())) rows += listFilesRecursive(fullPath.c_str(), depth + 1);
    } else if (strstr(file.path(), ".ild") || strstr(file.path(), ".ILD")) {
      // Skip any hidden files that start with dot
      if (file.name()[0] == '.') {
        file.close();
        continue;
      }
      rows += "<tr data-filename='" + fullPath + "'>";
      rows += "<td>" + fullPath + "</td>";
      rows += "<td>" + String(file.size()) + " bytes</td>";
      rows += "</tr>";
    }
    file.close();

    // Yield control every 2 files to prevent watchdog timeout
    fileCount++;
    if (fileCount % 2 == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      esp_task_wdt_reset(); // Reset watchdog
    }
  }
  dir.close();
  return rows;
}

void SDCard::refreshFileCache() {
  // Check if SD card is available before attempting to read
  if (!isAvailable()) {
    Serial.println("SD card not available for file cache refresh");
    cachedFileRows = "<tr><td colspan='2'>SD Card Missing.... insert card and refresh page</td></tr>";
    lastCacheUpdate = millis();
    return;
  }

  // Reset watchdog before potentially long operation
  esp_task_wdt_reset();

  cachedFileRows = listFilesRecursive("/", 0);
  lastCacheUpdate = millis();

  // If the result is empty, show appropriate message but don't mark SD as unavailable
  if (cachedFileRows.isEmpty()) {
    cachedFileRows = "<tr><td colspan='2'>SD Card Missing.... insert card and refresh page</td></tr>";
    Serial.println("File cache empty - no files found or scan timed out");
  } else {
    Serial.println("File cache refreshed successfully");
  }
}

String SDCard::generateFileRows() {
  unsigned long currentTime = millis();

  // Always check availability first to catch SD card removal immediately
  bool available = isAvailable();

  // If SD card is not mounted, use shorter cache duration and check more frequently
  unsigned long cacheDuration = available ? CACHE_DURATION_MS : CACHE_DURATION_UNAVAILABLE_MS;

  // Check if cache is empty or expired
  if (cachedFileRows.isEmpty() ||
      (currentTime - lastCacheUpdate) > cacheDuration) {
    refreshFileCache();
  }

  return cachedFileRows;
}

void SDCard::read(const char* path) {
  Serial.printf("Reading file: %s\n", path);

  if (!isAvailable()) {
    Serial.println("SD card not available");
    return;
  }

  File file = SD.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

File SDCard::getFile(const char* path) {
  if (!isAvailable()) {
    return File();  // Return empty file handle
  }
  return SD.open(path);
}

bool SDCard::fileExists(const char* path) {
  if (!isAvailable() || !SD.exists(path)) return false;
  File f = SD.open(path);
  bool isFile = f && !f.isDirectory();
  if (f) f.close();
  return isFile;
}

bool SDCard::checkAndMount() {
  // Prevent too frequent mount attempts
  unsigned long currentTime = millis();
  if (currentTime - lastMountAttempt < MOUNT_COOLDOWN_MS) {
    return sdCardMounted;
  }
  lastMountAttempt = currentTime;

  // Try to mount the SD card (single attempt, no retries)
  if (SD.begin(PIN_SD_CS, SDSPI)) {
    sdCardMounted = true;
    Serial.println("SD card mounted successfully");
    // Clear cache to force refresh with new card contents
    cachedFileRows = "";
    lastCacheUpdate = 0;
    return true;
  }

  sdCardMounted = false;
  Serial.println("SD card mount failed - no card present");
  return false;
}

bool SDCard::isAvailable() {
  // Simple check - don't perform any I/O operations
  return sdCardMounted;
}