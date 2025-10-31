
/*
  Nicla Sense ME — Movement-Triggered Logging (LittleFS)
  + Log rotation & GC when SPI Flash is nearly full
  + FIX: Acc fallback uses g-units; robust AR text parsing
*/

struct SampleRow;  // forward declaration
// Mbed LittleFS / BlockDevice, etc.
#include <BlockDevice.h>
#include <Dir.h>
#include <File.h>
#include <FileSystem.h>
#include <LittleFileSystem.h>

// C/C++ standard libraries
#include <cstdio>
#include <cstring>
#include <cmath>

// ArduinoBLE (for NUS-style BLE file transfer)
#include <ArduinoBLE.h>

// Sensors (BHY2: accelerometer/environment/activity recognition)
#include <Arduino_BHY2.h>

// =============================== Constants & Macros ================================
// —— BLE UUID (Nordic UART Service style, compatible with desktop reuse)
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (no response)
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify

// —— BLE packet size (default 20 bytes; can increase to ATT MTU-3 after negotiation)
static const size_t BLE_CHUNK = 20;

// —— LittleFS root directory and log directory
constexpr auto userRoot {"fs"};
static const char* LOG_DIR = "/logs"; // Log directory under the LittleFS root

// —— Segmentation & GC policy
static const size_t  SEG_MAX_BYTES         = 64*1024;   // Max bytes per segment (~64KB)
static const size_t  GC_MIN_FREE_BYTES     = 128*1024;  // Trigger GC when free space < this
static const size_t  GC_TARGET_FREE_BYTES  = 256*1024;  // Target free space after GC

// —— Debug print throttling
#define APPEND_DEBUG    1 // Set 0 to disable periodic serial print while writing logs
#define APPEND_EVERY_N  20 // Print every N writes

// —— Acceleration/activity fallback (all units unified in g)
static const float G_CONST = 9.80665f;     // m/s^2 to g conversion constant
static const float A_MAG_G = 1.0f;         // Acceleration magnitude when stationary (g)
static const float MOVE_THRESH = 0.15f;    // Deviation threshold (g)
static const uint32_t MOVE_EXIT_MS = 12000; // Fallback: exit hold after no motion (was 60s → 12s)

// —— Activity Recognition (AR) silence fallback
static const uint32_t AR_SILENCE_MS = 3000; // Use accel fallback if no AR event for this long

// —— “Silent window” after entering transfer mode
static const uint32_t TRANSFER_SILENCE_MS = 1500; // 1.5s window to let central discover services

// ============================== Global Objects & Variables ================================
// —— LittleFS object
mbed::BlockDevice* spif = nullptr;
mbed::LittleFileSystem fs {userRoot};

BLEService        nusService(NUS_SERVICE_UUID);
BLECharacteristic nusTx(NUS_TX_UUID, BLENotify, BLE_CHUNK,false);
BLECharacteristic nusRx(NUS_RX_UUID, BLEWrite | BLEWriteWithoutResponse, BLE_CHUNK,false);

// —— BLE transfer control
volatile bool bleAbort = false;   // Abort flag (set when client sends ABORT)

// —— Mode state machine
enum Mode : uint8_t { MODE_LOGGING=0, MODE_CONFIG=1, MODE_TRANSFER=2 };
volatile Mode g_mode = MODE_LOGGING;
volatile bool g_centralConnected = false;
uint32_t g_transferEnterMs = 0;   // Timestamp when transfer mode entered, for silent window

// —— Sampling / activity recognition
SensorXYZ accel(SENSOR_ID_ACC);
Sensor temp(SENSOR_ID_TEMP);
Sensor gas(SENSOR_ID_GAS);
SensorActivity activityAR(SENSOR_ID_AR);

uint32_t lastArEventMs = 0;   // Timestamp of the last AR event
uint32_t nextEnvSampleMs = 0; // Time for the next environmental sample

// Activity types and text mapping
enum Activity : uint8_t {
  ACT_STILL=0,
  ACT_WALK=1,
  ACT_RUN=2,
  ACT_BIKE=3,
  ACT_VEHICLE=4,
  ACT_TILT=5,
  ACT_VEHICLE_STILL=6,
  ACT_UNKNOWN=255
};

static inline const char* activityToStr(Activity a){
  switch(a){
    case ACT_STILL:          return "still";
    case ACT_WALK:           return "walking";
    case ACT_RUN:            return "running";
    case ACT_BIKE:           return "bike";
    case ACT_VEHICLE:        return "vehicle";
    case ACT_TILT:           return "tilting";
    case ACT_VEHICLE_STILL:  return "vehicle_still";
    default:                 return "unknown";
  }
}

// Sampling rates (activity-adaptive)
struct Rate{ uint32_t gasTempMs; uint32_t imuHz; };
Rate rateStill{15000,25};
Rate rateWalk{1500,50};
Rate rateRun{800,50};
Rate rateBike{1000,50};
Rate rateVehicle{1000,50};
Rate rateVehicleStill{1000,50};
Rate rateTilt{1500,50};

// Activity state debounce (enter: 2s, exit hold: 12s)
Activity curAct=ACT_STILL, pendingAct=ACT_STILL;
uint32_t actStableUntilMs=0;
static const uint32_t ENTER_DEBOUNCE_MS=2000; // Debounce time when entering an activity
static const uint32_t EXIT_HOLD_MS=12000;     // Hold time after activity ends

// Pre-trigger ring buffer
struct SampleRow { uint32_t ms; Activity act; float gas,tempC,ax,ay,az,a_rms; };
constexpr size_t RING_N=8;
SampleRow ring[RING_N];
size_t ringHead=0;
bool ringFilled=false;

// Current log segment information
char          curSegPath[48]        = {0};       // Current segment path, e.g. "logs/log_xxxxx.csv"
uint32_t      curSegIdx             = 0;         // Current segment index
size_t        curSegBytes           = 0;         // Bytes written in current segment (including header)

// ======================== Forward Declarations =================================
static void onBleConnected(BLEDevice central);
static void onBleDisconnected(BLEDevice central);
static void onBleRxWritten(BLEDevice central, BLECharacteristic ch);
static bool bleNotifyBytes(const uint8_t* data, size_t len);
static bool bleNotifyLine(const char* s);
static void listLogsOverBle();
static void bleSendFile(const char* filename);

// —— File system / logging
static void ensureLogDir();
static void makeSegPath(uint32_t idx);
static bool scanMinMaxIdx(uint32_t& minIdx, uint32_t& maxIdx, uint32_t& count);
static size_t getFileSize(const char* path);
static void ensureSegHeader();
static void rotateSegment();
static bool deleteOldestSegment();
static uint64_t fs_free_bytes();
static void checkSpaceAndGC();
static void appendCSV(const SampleRow& s);
static void dumpRingToFile();
static void printStats();

// —— Activity recognition / fallback
static void switchToPending(Activity a, uint32_t now);
static void onActivityEvent(Activity a, bool started);
static void maybeApplyPending(uint32_t now);
static void fallbackMotionUpdate(float a_mag_mps2, uint32_t now); // parameter: input in m/s^2
static void handleRawActivityEvent(uint8_t code);
static uint8_t decodeArCode(const String& s);
static void pumpArEventIfAny();

// —— Mode switching
static inline void enterTransferMode();
static inline void leaveTransferMode();

// ===================== Ring Buffer Utility ==========================
static void ringPush(const SampleRow& s){
  ring[ringHead]=s;
  ringHead=(ringHead+1)%RING_N;
  if(ringHead==0) ringFilled=true;
}

// ====================== Mode Switching ===============================
static inline void enterTransferMode(){
  g_mode = MODE_TRANSFER;
  g_transferEnterMs = millis();
  // When entering transfer mode: pause logging (delay next sampling indefinitely and skip GC)
  nextEnvSampleMs = UINT32_MAX;
}

static inline void leaveTransferMode(){
  g_mode = MODE_LOGGING;
  // When leaving transfer mode: resume sampling after 0.5s
  nextEnvSampleMs = millis() + 500;
}

// ====================== File System: Space, Directory, Segments ===========================================
// Tool: calculate remaining free space in bytes
static uint64_t fs_free_bytes(){
  struct statvfs st{};
  fs.statvfs("/", &st);
  return (uint64_t)st.f_bfree * (uint64_t)st.f_bsize;
}

static void ensureLogDir(){
  // Return if directory already exists
  mbed::Dir d;
  if (d.open(&fs, LOG_DIR) == 0) { d.close(); return; }
  // Create directory (-EEXIST considered success)
  int mk = fs.mkdir(LOG_DIR, 0777);
  if (mk != 0 && mk != -EEXIST){
    Serial.print("mkdir "); Serial.print(LOG_DIR);
    Serial.print(" failed: "); Serial.println(mk);
  }
}

static void makeSegPath(uint32_t idx){
  snprintf(curSegPath, sizeof(curSegPath), "%s/log_%05lu.csv", LOG_DIR, (unsigned long)idx);
}

// Tool: scan logs directory to find min/max segment index
static bool scanMinMaxIdx(uint32_t& minIdx, uint32_t& maxIdx, uint32_t& count){
  minIdx = 0xFFFFFFFFu; maxIdx = 0; count = 0;

  mbed::Dir dir;
  if (dir.open(&fs, LOG_DIR) != 0){
    // Directory not found or cannot be opened → treat as no segments
    minIdx = maxIdx = 0;
    return false;
  }

  dirent ent;
  while (dir.read(&ent) > 0){
    unsigned long idx=0;
    if (sscanf(ent.d_name, "log_%lu.csv", &idx) == 1){
      if (idx < minIdx) minIdx = idx;
      if (idx > maxIdx) maxIdx = idx;
      count++;
    }
  }
  dir.close();

  if (count==0){ minIdx=0; maxIdx=0; return false; }
  return true;
}

// Tool: get file size
static size_t getFileSize(const char* path){
  mbed::File f;
  if (f.open(&fs, path, O_RDONLY)) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

// Write CSV header if current segment is empty
void ensureSegHeader(){
  mbed::File f;
  if (f.open(&fs, curSegPath, O_RDONLY) == 0){
    size_t s = f.size(); f.close();
    if (s > 0){ curSegBytes = s; return; }
  }
  if (f.open(&fs, curSegPath, O_WRONLY | O_CREAT | O_APPEND) == 0){
    const char* header = "ms,activity,gas,tempC,ax,ay,az,a_rms\r\n";
    f.write(header, strlen(header)); f.close();
    curSegBytes = strlen(header);
  }
}

// Rotate to a new segment
static void rotateSegment(){
  curSegIdx += 1;
  makeSegPath(curSegIdx);
  curSegBytes = 0;
  ensureSegHeader();
  Serial.print("Rotated to "); Serial.println(curSegPath);
}

// Delete oldest segment (returns true if deleted)
bool deleteOldestSegment(){
  uint32_t minIdx, maxIdx, count;
  if (!scanMinMaxIdx(minIdx, maxIdx, count)) return false;
  if (count==0) return false;
  if (minIdx == curSegIdx){
    if (count == 1) return false;
    minIdx++;
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/log_%05lu.csv", LOG_DIR, (unsigned long)minIdx);
  int r = fs.remove(path);
  if (r==0){ Serial.print("GC removed "); Serial.println(path); return true; }
  else { Serial.print("GC remove failed "); Serial.println(path); return false; }
}

// Check available space and perform GC; disabled while central device is connected
static void checkSpaceAndGC(){
  if (g_centralConnected) return;

  uint64_t freeB = fs_free_bytes();
  if (freeB >= GC_MIN_FREE_BYTES) return;

  Serial.print("Low space: "); Serial.println((unsigned long)freeB);

  if (curSegBytes > (SEG_MAX_BYTES/2)){
    rotateSegment();
  }
  uint8_t guard = 32;
  while (fs_free_bytes() < GC_TARGET_FREE_BYTES && guard--){
    if (!deleteOldestSegment()){
      Serial.println("GC cannot free more (only current segment left?).");
      break;
    }
  }
}

// Append one row to current segment (with space check + rotation)
static void appendCSV(const SampleRow& s){
  // 1) Perform GC if space low
  checkSpaceAndGC();

  // 2) Rotate segment if size exceeds limit
  if (curSegBytes >= SEG_MAX_BYTES){
    rotateSegment();
  }

  // 3) Check again if space critically low
  if (fs_free_bytes() < 1024){
    Serial.println("FLASH FULL — skip append");
    return;
  }

  // 4) Append to file
  mbed::File f;
  if (f.open(&fs, curSegPath, O_WRONLY | O_CREAT | O_APPEND)){
    Serial.println("open append failed"); return;
  }
  char buf[160];
  int n = snprintf(buf, sizeof(buf), "%lu,%s,%.3f,%.2f,%.3f,%.3f,%.3f,%.3f\r\n",
                   (unsigned long)s.ms, activityToStr(s.act),
                   s.gas, s.tempC, s.ax, s.ay, s.az, s.a_rms);
  static uint32_t lineCount = 0;
  if (n > 0){
    int w = f.write(buf, n);
    if (w > 0){
      curSegBytes += (size_t)w;
      lineCount++;

#if APPEND_DEBUG
      if ((lineCount % APPEND_EVERY_N) == 0){
        Serial.print("[APPEND] seg_idx=");
        Serial.print(curSegIdx);
        Serial.print(" file=");
        Serial.print(curSegPath);
        Serial.print(" lines=");
        Serial.print(lineCount);
        Serial.print(" bytes=");
        Serial.print((unsigned long)curSegBytes);
        Serial.print(" free=");
        Serial.println((unsigned long)fs_free_bytes());
      }
#endif
    }
  }
  f.close();
}

// Dump pre-trigger buffer to file
void dumpRingToFile(){
  if (!ringFilled && ringHead==0) return;
  mbed::File f;
  if (f.open(&fs, curSegPath, O_WRONLY | O_CREAT | O_APPEND)){
    Serial.println("open append (ring) failed"); return;
  }
  size_t start = ringFilled ? ringHead : 0;
  size_t count = ringFilled ? RING_N : ringHead;
  for (size_t i=0; i<count; ++i){
    size_t idx=(start+i)%RING_N;
    const SampleRow& s=ring[idx];
    char buf[160];
    int n=snprintf(buf,sizeof(buf),"%lu,%s,%.3f,%.2f,%.3f,%.3f,%.3f,%.3f\r\n",
                   (unsigned long)s.ms, activityToStr(s.act),
                   s.gas, s.tempC, s.ax, s.ay, s.az, s.a_rms);
    if (n>0){ f.write(buf,n); curSegBytes += (size_t)n; }
  }
  f.close();
}

// Print storage statistics
static void printStats(){
  struct statvfs st{}; fs.statvfs("/", &st);
  auto b=st.f_bsize;
  Serial.print("Total: "); Serial.println(st.f_blocks*b);
  Serial.print("Free : "); Serial.println(st.f_bfree*b);
  Serial.print("Used : "); Serial.println((st.f_blocks-st.f_bfree)*b);
}

// ========================= Activity Recognition: Events, Debounce, Fallback ===========================
static void switchToPending(Activity a, uint32_t now){
  pendingAct=a;
  actStableUntilMs=now+ENTER_DEBOUNCE_MS;
}

static void onActivityEvent(Activity a, bool started){
  uint32_t now=millis();
  if (started){ switchToPending(a, now); }
  else{
    if (curAct==a){
      actStableUntilMs = max(actStableUntilMs, now+EXIT_HOLD_MS);
      pendingAct = ACT_STILL;
    }
  }
}

static void maybeApplyPending(uint32_t now){
  if ((int32_t)(now - actStableUntilMs) >= 0){
    if (pendingAct != curAct){
      Activity prev = curAct; curAct = pendingAct;
      if (prev==ACT_STILL && curAct!=ACT_STILL){ dumpRingToFile(); }
      Serial.print("Activity -> "); Serial.println(activityToStr(curAct));
    }
    actStableUntilMs = UINT32_MAX;
  }
}

// ====== Fix: fallback now uses g-units to determine deviation from gravity ======
static void fallbackMotionUpdate(float a_mag_mps2, uint32_t now){
  float a_mag_g = a_mag_mps2 / G_CONST;
  float dev = fabs(a_mag_g - A_MAG_G);

  if (dev > MOVE_THRESH){
    lastArEventMs = now;
    if (curAct == ACT_STILL){ onActivityEvent(ACT_WALK, true); }
  } else {
    if (curAct != ACT_STILL && (now - lastArEventMs > MOVE_EXIT_MS)){
      onActivityEvent(ACT_WALK, false);
    }
  }
}

// [continues unchanged…]
