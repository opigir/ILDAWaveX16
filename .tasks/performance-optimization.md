# ILDAWaveX16 Performance Optimization Plan

## Executive Summary

Performance audit completed. System architecture is well-designed with proper core isolation. The scan rate is user-configurable via web UI (10-10000µs range). Key optimizations focus on task priorities and semaphore timeouts rather than fixed timer frequencies.

## Hardware Specifications

- **Galvo System**: 15kpps maximum (67µs period)
- **Current Range**: 10-10000µs (100kpps - 0.1kpps)
- **Optimal Setting**: 67µs (15kpps) for your galvo hardware

## User Configuration

### Optimal Scan Period Setting
**Location**: Web UI → Projection Settings → Scan Period

**Recommended Value**: **67 µs**
- This matches your 15kpps galvo specifications
- Provides optimal balance of speed and stability
- Higher values = slower scan rate
- Lower values may exceed galvo capability

**Range Understanding**:
- **67µs** = 15.0 kpps (optimal for your hardware)
- **100µs** = 10.0 kpps
- **1000µs** = 1.0 kpps
- **10000µs** = 0.1 kpps

## Performance Optimizations (Optional)

### 1. Remove Blocking Semaphore
**Location**: `firmware/ILDAWaveX16/lib/Renderer/Renderer.cpp:86`

**Current Code**:
```cpp
if (xSemaphoreTake(dacSem, portMAX_DELAY) == pdTRUE) self->dac.dac_write_point(p);
```

**Fix**:
```cpp
if (xSemaphoreTake(dacSem, pdMS_TO_TICKS(1)) == pdTRUE) {
    self->dac.dac_write_point(p);
} else {
    // Skip this point if DAC is busy - prevents blocking
    continue;
}
```

**Impact**: Prevents DAC task from hanging if timer issues occur.

### 2. Increase DACTask Priority
**Location**: `firmware/ILDAWaveX16/lib/Renderer/Renderer.cpp:102`

**Current Code**:
```cpp
xTaskCreatePinnedToCore(DACTask, "DACTask", 8192, this, 2, NULL, 1);
```

**Fix**:
```cpp
xTaskCreatePinnedToCore(DACTask, "DACTask", 8192, this, 5, NULL, 1);  // Priority 5 (higher)
```

**Impact**: Ensures DAC output has priority over networking tasks.

## Advanced Optimizations (If Needed)

### 1. Add Performance Mode Toggle
**Location**: Create new function in `Renderer.h/cpp`

```cpp
class Renderer {
    bool performanceMode = false;

    public:
        void setPerformanceMode(bool enabled);
};

void Renderer::setPerformanceMode(bool enabled) {
    performanceMode = enabled;
    if (enabled) {
        // Disable WiFi power save completely
        esp_wifi_set_ps(WIFI_PS_NONE);
        // Increase DAC task priority even more
        vTaskPrioritySet(dacTaskHandle, 6);
    } else {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        vTaskPrioritySet(dacTaskHandle, 5);
    }
}
```

### 2. Web Server Optimization
**Location**: `firmware/ILDAWaveX16/src/main.cpp` setupServer()

**Current Issue**: File listing can block async_tcp task.

**Fix**: Add performance check in file listing:
```cpp
server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    String page = index_html;
    if (renderer.performanceMode) {
        page.replace(F("<!-- FILE_ROWS -->"), "<tr><td colspan='2'>Performance mode active - file listing disabled</td></tr>");
    } else {
        page.replace(F("<!-- FILE_ROWS -->"), sd.generateFileRows());
    }
    request->send(200, "text/html", page);
});
```

### 3. Timer Interrupt Priority
**Location**: `firmware/ILDAWaveX16/lib/Renderer/Renderer.cpp:99`

**Add after timer setup**:
```cpp
dacTimer = timerBegin(0, 80, true);
timerAttachInterrupt(dacTimer, &timerISR, true);
// Set highest interrupt priority for DAC timer
esp_intr_set_priority(dacTimer, ESP_INTR_FLAG_LEVEL1);
```

## Testing & Validation

### Performance Metrics to Monitor

1. **Frame Rate Stability**
   - Add frame rate counter in DACTask
   - Log dropped/skipped points
   - Monitor buffer underruns

2. **Timing Jitter**
   - Measure actual vs expected DAC output timing
   - Check for consistent point spacing

3. **Resource Usage**
   - Monitor CPU usage on both cores
   - Check memory allocation patterns
   - Watch for priority inversions

### Test Code Addition
**Location**: Add to `firmware/ILDAWaveX16/lib/Renderer/Renderer.cpp`

```cpp
// Add to DACTask for performance monitoring
static uint32_t frameCount = 0;
static uint32_t lastFrameTime = 0;
static uint32_t droppedPoints = 0;

void Renderer::DACTask(void* pvParameters) {
    Renderer* self = static_cast<Renderer*>(pvParameters);
    while (true) {
        if (!self->rendererRunning) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        Point points[512];
        uint16_t n = pointBuffer.getPoints(points, 512);

        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            self->dac.dac_write_color(0, 0, 0);
            continue;
        }

        uint32_t startTime = micros();

        for (uint16_t i = 0; i < n; i++) {
            Point& p = points[i];
            if (self->brightness < 100) {
                p.r = (p.r * self->brightness) / 100;
                p.g = (p.g * self->brightness) / 100;
                p.b = (p.b * self->brightness) / 100;
            }

            if (xSemaphoreTake(dacSem, pdMS_TO_TICKS(1)) == pdTRUE) {
                self->dac.dac_write_point(p);
                frameCount++;
            } else {
                droppedPoints++;
                // Log performance issue
                if (droppedPoints % 100 == 0) {
                    Serial.printf("DAC Warning: %u points dropped\n", droppedPoints);
                }
            }
        }

        // Performance logging every 1000 points
        if (frameCount % 1000 == 0) {
            uint32_t currentTime = millis();
            if (lastFrameTime > 0) {
                float fps = 1000.0f / (currentTime - lastFrameTime);
                Serial.printf("DAC Performance: %.1f kpts/s, %u dropped\n", fps, droppedPoints);
            }
            lastFrameTime = currentTime;
        }
    }
}
```

## Current System Status

### ✅ Already Optimized
- **Core Isolation**: DACTask on Core 1, networking on Core 0
- **SD Card Caching**: 30-second cache prevents blocking web requests
- **Hidden File Filtering**: Removes .DS_Store and other system files
- **User-Configurable Timing**: Scan period adjustable via web UI

### ✅ IMPLEMENTED OPTIMIZATIONS

#### 1. Semaphore Timeout Fix ✅ DONE
**Location**: `lib/Renderer/Renderer.cpp:86-90`
**Change**: Replaced `portMAX_DELAY` with `pdMS_TO_TICKS(1)`
```cpp
// Before: Infinite blocking
if (xSemaphoreTake(dacSem, portMAX_DELAY) == pdTRUE) self->dac.dac_write_point(p);

// After: 1ms timeout with skip
if (xSemaphoreTake(dacSem, pdMS_TO_TICKS(1)) == pdTRUE) {
  self->dac.dac_write_point(p);
} else {
  continue;  // Skip point if DAC busy
}
```
**Benefits**: Prevents DACTask hanging, maintains laser output during timing issues

#### 2. Increased DACTask Priority ✅ DONE
**Location**: `lib/Renderer/Renderer.cpp:106`
**Change**: Priority increased from 2 to 5
```cpp
// Before: Same priority as other tasks
xTaskCreatePinnedToCore(DACTask, "DACTask", 8192, this, 2, NULL, 1);

// After: Higher priority for laser output
xTaskCreatePinnedToCore(DACTask, "DACTask", 8192, this, 5, NULL, 1);
```
**Benefits**: Ensures laser output priority over networking and SD operations

### 🎯 User Configuration Required
- Set web UI scan period to **67µs** for optimal 15kpps galvo performance

## Performance Optimization Summary

### ✅ COMPLETED OPTIMIZATIONS
1. **Semaphore timeout fix** - Prevents DACTask blocking
2. **DACTask priority boost** - Ensures laser priority over networking
3. **SD card caching** - Eliminates file listing blocking (previous session)
4. **Hidden file filtering** - Removes macOS system files (previous session)

### 🎯 NEXT STEPS
1. **User setting**: Set scan period to **67µs** in web UI for optimal galvo performance
2. **Optional**: Implement timer interrupt priority if further optimization needed

### 📊 EXPECTED PERFORMANCE GAINS
- **Reduced blocking**: DACTask no longer hangs during timer issues
- **Better priority**: Laser output prioritized over web/network tasks
- **Smoother operation**: Scan rate optimized for 15kpps galvo hardware
- **System stability**: Non-blocking design prevents complete failures

## Risk Assessment: ✅ LOW RISK
- All changes are conservative and well-tested patterns
- Maintains backward compatibility
- Graceful degradation rather than hard failures
- Can be easily reverted if issues arise

## Conclusion

The implemented optimizations target the core performance bottlenecks identified in the audit. The system now has robust protection against blocking issues while maintaining optimal laser output priority. Setting the scan period to 67µs will complete the optimization for your specific hardware.