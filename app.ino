
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

// Sensors (BHY2: accelerometer / environment / activity recognition)
#include <Arduino_BHY2.h>

// =============================== Constants & Macros ================================
// —— BLE UUID (Nordic UART Service style, convenient for desktop reuse)
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (no response)
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify

// —— BLE fragment length (default 20 bytes; once stable, can increase to negotiated ATT MTU - 3)
static const size_t BLE_CHUNK = 20;

// —— LittleFS root and log directory
constexpr auto userRoot {"fs"};
static const char* LOG_DIR = "/logs"; // Log directory under LittleFS root

// —— Segmentation & GC policy
static const size_t  SEG_MAX_BYTES         = 64*1024;   // Max bytes per segment (~64KB)
static const size_t  GC_MIN_FREE_BYTES     = 128*1024;  // Trigger GC when free space falls below this
static const size_t  GC_TARGET_FREE_BYTES  = 256*1024;  // After GC, ensure at least this much free space

// —— Debug output throttling
#define APPEND_DEBUG    1 // Set to 0 to disable periodic serial prints during logging
#define APPEND_EVERY_N  20 // Print once every N appended lines

// —— Acceleration/activity fallback (all units unified to g)
static const float G_CONST = 9.80665f;     // Conversion constant from m/s^2 to g
static const float A_MAG_G = 1.0f;         // Acceleration magnitude at rest (g)
static const float MOVE_THRESH = 0.15f;    // Deviation threshold (g)
static const uint32_t MOVE_EXIT_MS = 12000; // Fallback: exit hold after no motion (reduced from 60s to 12s)

// —— Activity Recognition (AR) silence fallback
static const uint32_t AR_SILENCE_MS = 3000; // If AR has been silent longer than this, enable accel-based fallback

// Silent window after entering transfer mode
static const uint32_t TRANSFER_SILENCE_MS = 1500; // Do not operate FS for the first 1.5s after connection; allow central to discover services

// ============================== Globals & State ================================
// —— LittleFS objects
mbed::BlockDevice* spif = nullptr;
mbed::LittleFileSystem fs {userRoot};

BLEService        nusService(NUS_SERVICE_UUID);
BLECharacteristic nusTx(NUS_TX_UUID, BLENotify, BLE_CHUNK,false);
BLECharacteristic nusRx(NUS_RX_UUID, BLEWrite | BLEWriteWithoutResponse, BLE_CHUNK,false);

// —— BLE transfer control
volatile bool bleAbort = false;   // Abort flag (client sends ABORT)

// —— Mode state machine
enum Mode : uint8_t { MODE_LOGGING=0, MODE_CONFIG=1, MODE_TRANSFER=2 };
volatile Mode g_mode = MODE_LOGGING;
volatile bool g_centralConnected = false;
uint32_t g_transferEnterMs = 0;   // Timestamp for entering transfer mode, used for the initial connection silent window

// —— Sampling / activity recognition
SensorXYZ accel(SENSOR_ID_ACC);
Sensor temp(SENSOR_ID_TEMP);
Sensor gas(SENSOR_ID_GAS);
SensorActivity activityAR(SENSOR_ID_AR);

uint32_t lastArEventMs = 0;   // Time of the most recent AR event
uint32_t nextEnvSampleMs = 0; // Time for the next environmental sample

// Activity types and string mapping
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

// Activity state debounce (parameters tuned: enter 2s, exit hold 12s)
Activity curAct=ACT_STILL, pendingAct=ACT_STILL;
uint32_t actStableUntilMs=0;
static const uint32_t ENTER_DEBOUNCE_MS=2000; // Debounce when entering an activity (was 3s → 2s)
static const uint32_t EXIT_HOLD_MS=12000;     // Hold time after an activity ends (was 30s → 12s)

// Pre-trigger ring buffer
struct SampleRow { uint32_t ms; Activity act; float gas,tempC,ax,ay,az,a_rms; };
constexpr size_t RING_N=8;
SampleRow ring[RING_N];
size_t ringHead=0;
bool ringFilled=false;

// Current log segment info
char          curSegPath[48]        = {0};       // Current segment full path "logs/log_xxxxx.csv"
uint32_t      curSegIdx             = 0;         // Current segment index
size_t        curSegBytes           = 0;         // Bytes written in current segment (including header)

// ======================== Forward Declarations =================================
// —— BLE events / utilities
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

// ===================== Utility: Ring Buffer ==========================
static void ringPush(const SampleRow& s){
  ring[ringHead]=s;
  ringHead=(ringHead+1)%RING_N;
  if(ringHead==0) ringFilled=true;
}

// ====================== Mode Switching ===============================
static inline void enterTransferMode(){
  g_mode = MODE_TRANSFER;
  g_transferEnterMs = millis();
  // Upon entering transfer mode: pause logging (push the next sampling far out, and do not trigger GC)
  nextEnvSampleMs = UINT32_MAX;
}

static inline void leaveTransferMode(){
  g_mode = MODE_LOGGING;
  // Upon leaving transfer mode: resume sampling after 0.5s
  nextEnvSampleMs = millis() + 500;
}

// ====================== File System: Space / Directory / Segments ===========================================
// Utility: compute remaining free space (bytes)
static uint64_t fs_free_bytes(){
  struct statvfs st{};
  fs.statvfs("/", &st);
  return (uint64_t)st.f_bfree * (uint64_t)st.f_bsize;
}

static void ensureLogDir(){
  // If directory exists, return
  mbed::Dir d;
  if (d.open(&fs, LOG_DIR) == 0) { d.close(); return; }
  // Create directory (-EEXIST is considered success)
  int mk = fs.mkdir(LOG_DIR, 0777);
  if (mk != 0 && mk != -EEXIST){
    Serial.print("mkdir "); Serial.print(LOG_DIR);
    Serial.print(" failed: "); Serial.println(mk);
  }
}

static void makeSegPath(uint32_t idx){
  snprintf(curSegPath, sizeof(curSegPath), "%s/log_%05lu.csv", LOG_DIR, (unsigned long)idx);
}

// Utility: scan logs directory and find existing min/max segment indices
static bool scanMinMaxIdx(uint32_t& minIdx, uint32_t& maxIdx, uint32_t& count){
  minIdx = 0xFFFFFFFFu; maxIdx = 0; count = 0;

  mbed::Dir dir;
  if (dir.open(&fs, LOG_DIR) != 0){
    // Directory does not exist or cannot be opened => treat as no segment files
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

// Utility: read file size
static size_t getFileSize(const char* path){
  mbed::File f;
  if (f.open(&fs, path, O_RDONLY)) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

// Write CSV header in the current segment (if empty)
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

// Delete the oldest segment (return whether a segment was deleted)
bool deleteOldestSegment(){
  uint32_t minIdx, maxIdx, count;
  if (!scanMinMaxIdx(minIdx, maxIdx, count)) return false;
  if (count==0) return false; // Directory empty
  if (minIdx == curSegIdx){ // If the oldest is the current writing segment
    if (count == 1) return false; // Only one segment exists; cannot delete
    minIdx++; // Otherwise delete the second-oldest to protect current writing segment
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/log_%05lu.csv", LOG_DIR, (unsigned long)minIdx);
  int r = fs.remove(path);
  if (r==0){ Serial.print("GC removed "); Serial.println(path); return true; }
  else { Serial.print("GC remove failed "); Serial.println(path); return false; }
}

// Check space and perform GC; only allowed when NOT connected to a central device
static void checkSpaceAndGC(){
  if (g_centralConnected) return; // Disable GC while connected to avoid contention with BLE

  uint64_t freeB = fs_free_bytes();
  if (freeB >= GC_MIN_FREE_BYTES) return;

  Serial.print("Low space: "); Serial.println((unsigned long)freeB);

  if (curSegBytes > (SEG_MAX_BYTES/2)){
    rotateSegment();
  }
  uint8_t guard = 32; // Prevent infinite loop under extreme conditions
  while (fs_free_bytes() < GC_TARGET_FREE_BYTES && guard--){
    if (!deleteOldestSegment()){
      Serial.println("GC cannot free more (only current segment left?).");
      break;
    }
  }
}

// Append one row to the current segment (with “space check + rotation”)
static void appendCSV(const SampleRow& s){
  // 1) If space is low, run GC first
  checkSpaceAndGC();

  // 2) Rotate segment if current one exceeds the size limit
  if (curSegBytes >= SEG_MAX_BYTES){
    rotateSegment();
  }

  // 3) Double-check there is enough space (extreme cases)
  if (fs_free_bytes() < 1024){ // If critically low, skip to avoid corruption
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
  static uint32_t lineCount = 0;  // Counts only lines written by appendCSV (not ring dumps)
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

// Flush pre-trigger buffer to file
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

// Print capacity stats
static void printStats(){
  struct statvfs st{}; fs.statvfs("/", &st);   // Use "/" for the root mounted on fs
  auto b=st.f_bsize;
  Serial.print("Total: "); Serial.println(st.f_blocks*b);
  Serial.print("Free : "); Serial.println(st.f_bfree*b);
  Serial.print("Used : "); Serial.println((st.f_blocks-st.f_bfree)*b);
}

// ========================= Activity Recognition: events / debounce / fallback ===========================
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

// ====== Fix: fallback uses g-units consistently to determine deviation from gravity ======
static void fallbackMotionUpdate(float a_mag_mps2, uint32_t now){
  float a_mag_g = a_mag_mps2 / G_CONST;       // Convert m/s^2 to g
  float dev = fabs(a_mag_g - A_MAG_G);        // Deviation from 1g

  if (dev > MOVE_THRESH){
    lastArEventMs = now;
    if (curAct == ACT_STILL){ onActivityEvent(ACT_WALK, true); }
  } else {
    if (curAct != ACT_STILL && (now - lastArEventMs > MOVE_EXIT_MS)){
      onActivityEvent(ACT_WALK, false);
    }
  }
}

static void handleRawActivityEvent(uint8_t code){
  switch(code){
    // started
    case 8:  onActivityEvent(ACT_STILL,          true); break;
    case 9:  onActivityEvent(ACT_WALK,           true); break;
    case 10: onActivityEvent(ACT_RUN,            true); break;
    case 11: onActivityEvent(ACT_BIKE,           true); break;
    case 12: onActivityEvent(ACT_VEHICLE,        true); break;
    case 13: onActivityEvent(ACT_TILT,           true); break;
    case 14: onActivityEvent(ACT_VEHICLE_STILL,  true); break;

    // ended
    case 0:  onActivityEvent(ACT_STILL,          false); break;
    case 1:  onActivityEvent(ACT_WALK,           false); break;
    case 2:  onActivityEvent(ACT_RUN,            false); break;
    case 3:  onActivityEvent(ACT_BIKE,           false); break;
    case 4:  onActivityEvent(ACT_VEHICLE,        false); break;
    case 5:  onActivityEvent(ACT_TILT,           false); break;
    case 6:  onActivityEvent(ACT_VEHICLE_STILL,  false); break;

    default: break;
  }
}

// ====== Fix: case-insensitive + broader keywords ======
static uint8_t decodeArCode(const String& sIn) {
  if (sIn.length() == 0) return 255;

  String s = sIn;
  s.toLowerCase();

  bool started = (s.indexOf("started") >= 0);
  bool ended   = (s.indexOf("ended")   >= 0);

  // Match the longer "in vehicle still" first
  if (s.indexOf("in vehicle still") >= 0) {
    return started ? 14 : (ended ? 6 : 255);
  }
  // Other activity keywords
  if (s.indexOf("still") >= 0 && s.indexOf("vehicle") < 0) {
    return started ? 8 : (ended ? 0 : 255);
  }
  if (s.indexOf("walking") >= 0) {
    return started ? 9 : (ended ? 1 : 255);
  }
  if (s.indexOf("running") >= 0) {
    return started ? 10 : (ended ? 2 : 255);
  }
  if (s.indexOf("on bicycle") >= 0 || s.indexOf("bicycle") >= 0 || s.indexOf("bike") >= 0) {
    return started ? 11 : (ended ? 3 : 255);
  }
  if (s.indexOf("in vehicle") >= 0) {
    return started ? 12 : (ended ? 4 : 255);
  }
  if (s.indexOf("tilting") >= 0) {
    return started ? 13 : (ended ? 5 : 255);
  }
  return 255;
}

// Handle one AR event: de-duplicate and then dispatch into the state machine
void pumpArEventIfAny() {
  String s = activityAR.getActivity();     // Library returns strings like "... started/ended"
  static uint8_t lastCode = 255;           // De-dup to avoid retriggering
  uint8_t code = decodeArCode(s);
  if (code != 255 && code != lastCode) {
#if APPEND_DEBUG
    Serial.print("[AR] "); Serial.println(s);
#endif
    handleRawActivityEvent(code);
    lastCode = code;
    lastArEventMs = millis();
  }
}

// ========================= BLE: utilities & callbacks ===============================
static bool bleNotifyBytes(const uint8_t* data, size_t len){
  while (len > 0) {
    size_t n = len > BLE_CHUNK ? BLE_CHUNK : len;
    if (!nusTx.writeValue(data, n)) return false; // Fragmented notify
    data += n;
    len  -= n;
    delay(2); // Gentle throttling to avoid stack backlog; tune based on throughput
  }
  return true;
}

static bool bleNotifyLine(const char* s){
  return bleNotifyBytes((const uint8_t*)s, strlen(s));
}

// List /logs directory (format "F <filename> <bytes>\n", ends with "F END\n")
static void listLogsOverBle(){
  mbed::Dir dir;
  if (dir.open(&fs, LOG_DIR) != 0) {
    bleNotifyLine("#ERROR open_dir\n");
    return;
  }
  dirent ent;
  while (dir.read(&ent) > 0){
    // Only match log_*.csv
    if (strstr(ent.d_name, "log_") == ent.d_name && strstr(ent.d_name, ".csv")) {
      char path[64];
      snprintf(path, sizeof(path), "%s/%s", LOG_DIR, ent.d_name);
      size_t sz = getFileSize(path);

      char line[96];
      snprintf(line, sizeof(line), "F %s %lu\n", ent.d_name, (unsigned long)sz);
      bleNotifyLine(line);
    }
  }
  dir.close();
  bleNotifyLine("F END\n");  // End marker
}

// Send the given file (filename only, without path)
static void bleSendFile(const char* filename){
  printStats(); // Print board storage stats before sending
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", LOG_DIR, filename);
  Serial.print("[BleSendFile] 打开文件: "); Serial.print(path);

  mbed::File f;
  if (f.open(&fs, path, O_RDONLY) != 0) {
    Serial.print("[BleSendFile] 打开文件失败");
    bleNotifyLine("#ERROR open\n");
    return;
  }

  // BEGIN marker (includes size so the client can pre-create the file)
  size_t fsz = f.size();
  char head[128];
  snprintf(head, sizeof(head), "#BEGIN %s %lu\n", filename, (unsigned long)fsz);
  bleNotifyLine(head);

  // Read by CHUNK and notify
  uint8_t buf[BLE_CHUNK];
  bleAbort = false;
  while (!bleAbort) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    Serial.print("[bleSendFile] 发送分片, 字节数: "); Serial.println(n);
    if (!bleNotifyBytes(buf, (size_t)n)) {
      Serial.println("BLE notify failed.");
      break;
    }
  }
  f.close();
  Serial.print("[bleSendFile] 文件发送结束: "); Serial.println(filename);
  printStats();  // Print storage stats after sending

  bleNotifyLine("\n#END ");
  bleNotifyLine(filename);
  bleNotifyLine("\n");
}

// Connection/disconnection callbacks: enter CONFIG on connect, return to LOGGING on disconnect
static void onBleConnected(BLEDevice central){
  printStats();
  g_centralConnected = true;
  g_mode = MODE_CONFIG;
  Serial.println("[BLE] connected -> CONFIG_MODE");
  // Stop sampling & GC
  nextEnvSampleMs = UINT32_MAX;
}
static void onBleDisconnected(BLEDevice central){
  g_centralConnected = false;
  g_mode = MODE_LOGGING;
  Serial.println("[BLE] disconnected -> LOGGING");
  nextEnvSampleMs = millis() + 500; // Resume sampling 0.5s after disconnect
}

// RX write event: parse commands (List/GET<name>/ABORT)
void onBleRxWritten(BLEDevice central, BLECharacteristic ch){
  // 1) Read command into local buffer
  int len = ch.valueLength();
  if (len <= 0) return;

  static char cmd[160];
  len = min(len, (int)sizeof(cmd)-1);
  memcpy(cmd, ch.value(), len);
  cmd[len] = 0;

  // 2) Strip trailing \r \n to handle cases like "GET foo.csv\r\n"
  while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r')) {
    cmd[--len] = 0;
  }

  Serial.print("[BLE RX] ");
  Serial.println(cmd);

  // 3) Parse and execute
  if (strncmp(cmd, "CONFIG", 6) == 0) {
    g_mode = MODE_CONFIG;
    nextEnvSampleMs = UINT32_MAX;
    bleNotifyLine("OK CONFIG\n");
    Serial.println("[MODE] -> CONFIG (stop logging, wait for commands)");
    return;
  }

  if (strncmp(cmd, "STARTLOG", 8) == 0) {
    g_mode = MODE_LOGGING;
    nextEnvSampleMs = millis() + 500;
    bleNotifyLine("OK STARTLOG\n");
    Serial.println("[MODE] -> LOGGING (resume data logging)");
    return;
  }

  if (strncmp(cmd, "TRANSFER", 8) == 0) {
    g_mode = MODE_TRANSFER;
    g_transferEnterMs = millis();
    nextEnvSampleMs = UINT32_MAX;
    bleNotifyLine("OK TRANSFER\n");
    Serial.println("[MODE] -> TRANSFER (file transfer mode)");
    return;
  }

  if (strncmp(cmd, "LIST", 4) == 0) {
    listLogsOverBle();
    return;
  }

  if (strncmp(cmd, "GET ", 4) == 0) {
    const char* name = cmd + 4;
    while (*name == ' ') name++;

    if (*name == 0) {
      bleNotifyLine("#ERROR no_filename\n");
      return;
    }
    bleSendFile(name);
    return;
  }

  if (strncmp(cmd, "ABORT", 5) == 0) {
    bleAbort = true;
    Serial.println("[BLE RX] ABORT received, stopping transfer");
    return;
  }

  bleNotifyLine("#ERROR unknown_cmd\n");
}

// =========================== setup / loop =============================
void setup(){
  Serial.begin(115200);
  for (auto t=millis()+2500; !Serial && millis()<t; delay(100)) {}

  // ---- 1) Initialize BLE: service, characteristics, callbacks, start advertising ----
  if (!BLE.begin()) {
    Serial.println("BLE begin failed!");
  } else {
    // Connection interval (unit 1.25ms): 12*1.25=15ms, 24*1.25=30ms (balance power and compatibility)
    BLE.setConnectionInterval(12, 24);
    BLE.setLocalName("NiclaSenseME-Logger");
    BLE.setDeviceName("NiclaSenseME-Logger");

    BLE.setAdvertisedService(nusService);
    nusService.addCharacteristic(nusTx);
    nusService.addCharacteristic(nusRx);
    nusRx.setEventHandler(BLEWritten, onBleRxWritten);

    BLE.setEventHandler(BLEConnected,    onBleConnected);
    BLE.setEventHandler(BLEDisconnected, onBleDisconnected);

    BLE.addService(nusService);
    BLE.advertise();
    Serial.println("BLE advertising (NUS) started.");
  }

  delay(200); // Give the BLE subsystem a moment to stabilize (empirical)

  // ---- 2) LittleFS: mount & prepare segment files ----
  Serial.print("Mounting LittleFS...");
  spif = mbed::BlockDevice::get_default_instance();
  spif->init();
  int err = fs.mount(spif);
  if (err){
    Serial.print(" mount failed = "); Serial.println(err);
    Serial.println("Reformatting...");
    err = fs.reformat(spif);
    if (err){ Serial.print(" reformat failed = "); Serial.println(err); while(1){} }
  }
  Serial.println(" done.");

  ensureLogDir();

  uint32_t minIdx=0, maxIdx=0, cnt=0;
  bool hasAny = scanMinMaxIdx(minIdx, maxIdx, cnt);
  if (!hasAny){
    curSegIdx = 1;
    makeSegPath(curSegIdx);
    ensureSegHeader();
  } else {
    curSegIdx = maxIdx;
    makeSegPath(curSegIdx);
    curSegBytes = getFileSize(curSegPath);
    if (curSegBytes == 0) { ensureSegHeader(); }
    if (curSegBytes >= SEG_MAX_BYTES) { rotateSegment(); }
  }
  Serial.print("Active segment: "); Serial.println(curSegPath);

  // ---- 3) Sensors: initialize last to avoid concurrent init with BLE ----
  Serial.print("Init sensors...");
  BHY2.begin(NICLA_STANDALONE);
  accel.begin(); temp.begin(); gas.begin();
  activityAR.begin();
  Serial.println(" done.");

  nextEnvSampleMs   = millis() + 500;
  actStableUntilMs  = UINT32_MAX;
  lastArEventMs     = millis();

  g_mode = MODE_LOGGING;   // Start in logging mode
}

void loop(){
  BLE.poll();
  uint32_t now = millis();

  if (g_mode == MODE_CONFIG) {
    // CONFIG mode: stop sampling & logging; only process BLE RX/commands
    BHY2.update();
    pumpArEventIfAny();
    return;
  }
  else if (g_mode == MODE_TRANSFER) {
    if ((int32_t)(now - g_transferEnterMs) < TRANSFER_SILENCE_MS) {
      return;
    }
    return;
  }
  else { // MODE_LOGGING
    BHY2.update();
    pumpArEventIfAny();

    // If AR is silent >3s, enable fallback (decision in g-units)
    if ((int32_t)(now - lastArEventMs) > (int32_t)AR_SILENCE_MS) {
      float ax = accel.x(), ay = accel.y(), az = accel.z();    // Typically in m/s^2
      float a_mag_mps2 = sqrtf(ax*ax + ay*ay + az*az);
      fallbackMotionUpdate(a_mag_mps2, now);
    }

    maybeApplyPending(now);

    Rate r =
      (curAct==ACT_STILL)         ? rateStill :
      (curAct==ACT_WALK)          ? rateWalk  :
      (curAct==ACT_RUN)           ? rateRun   :
      (curAct==ACT_BIKE)          ? rateBike  :
      (curAct==ACT_VEHICLE)       ? rateVehicle :
      (curAct==ACT_VEHICLE_STILL) ? rateVehicleStill :
                                    rateTilt;

    if ((int32_t)(now - nextEnvSampleMs) >= 0) {
      float ax=accel.x(), ay=accel.y(), az=accel.z();
      float a_rms = sqrtf((ax*ax + ay*ay + az*az)/3.0f);
      float gasv = gas.value();
      float tempC = temp.value();

      SampleRow row{now, curAct, gasv, tempC, ax, ay, az, a_rms};
      ringPush(row);
      appendCSV(row);

      BHY2.update();

      nextEnvSampleMs = now + r.gasTempMs;
    }

    checkSpaceAndGC();
    return;
  }
}
