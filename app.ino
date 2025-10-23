
/*
  Nicla Sense ME — Movement-Triggered Logging (LittleFS)
  + Log rotation & GC when SPI Flash is nearly full
*/
 
 struct SampleRow;  // 前置声明
// Mbed LittleFS / BlockDeivce等
#include <BlockDevice.h>
#include <Dir.h>
#include <File.h>
#include <FileSystem.h>
#include <LittleFileSystem.h>

// C/C++ 标准库
#include <cstdio>
#include <cstring>

// ArduinoBLE (用于NUS风格的BLE文件传输)
#include <ArduinoBLE.h>

// 传感器（BHY2:加速度/环境/活动识别）
#include <Arduino_BHY2.h>

// ===============================常量与宏================================
// —— BLE UUID(Nordic UART Service 风格，便于桌面端复用
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (no response)
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify

// —— BLE 分片长度（默认20字节；稳定后可提升至协商后的ATT MTU-3）
static const size_t BLE_CHUNK = 20;

// —— LittleFS 根目录与日志目录
constexpr auto userRoot {"fs"};
static const char* LOG_DIR = "/logs"; // LittleFS 根目录下的日志目录

// —— 分段与GC策略
static const size_t  SEG_MAX_BYTES         = 64*1024;   // 每段最大字节数（约 64KB）
static const size_t  GC_MIN_FREE_BYTES     = 128*1024;  // 低于此剩余空间时触发 GC
static const size_t  GC_TARGET_FREE_BYTES  = 256*1024;  // GC 后至少回到这个余量

// —— 调试输出节流
#define APPEND_DEBUG    1 // 置0可关闭定期写入日志的串口打印
#define APPEND_EVERY_N  20 // 每写N行打印一次

// —— 活动识别兜底阈值
static const float A_MAG_G = 1.0f;          // 静止时加速度模长（g）
static const float MOVE_THRESH = 0.15f;    // 触发运动的偏离阈值
static const uint32_t MOVE_EXIT_MS = 60000; // 无运动后退出状态的保持时长

// —— 活动识别（AR）静默回退
static const uint32_t AR_SILENCE_MS = 3000; // AR事件静默超过此值后启用加速度兜底

// 传输模式进入后的“静默窗口”
static const uint32_t TRANSFER_SILENCE_MS = 1500; // 连接后前1.5秒不操作FS，给中央端时间发现服务

// ============================== 全局对象与变量 ================================
// —— LittleFS 对象
mbed::BlockDevice* spif = nullptr;  
mbed::LittleFileSystem fs {userRoot};

BLEService        nusService(NUS_SERVICE_UUID);
BLECharacteristic nusTx(NUS_TX_UUID, BLENotify, BLE_CHUNK,false);
BLECharacteristic nusRx(NUS_RX_UUID, BLEWrite | BLEWriteWithoutResponse, BLE_CHUNK,false);

// —— BLE 传输控制
volatile bool bleAbort = false;   // 中止标志（客户端发送ABORT）

// —— 模式状态机
enum Mode : uint8_t { MODE_LOGGING=0, MODE_TRANSFER=1 };
volatile Mode g_mode = MODE_LOGGING;
volatile bool g_centralConnected = false;
uint32_t g_transferEnterMs = 0;   // 进入传输模式的时间戳，用于“连接初期静默窗口”

// —— 采样/活动识别相关
SensorXYZ accel(SENSOR_ID_ACC);
Sensor temp(SENSOR_ID_TEMP);
Sensor gas(SENSOR_ID_GAS);
SensorActivity activityAR(SENSOR_ID_AR);

uint32_t lastArEventMs = 0; // 最近一次收到AR事件的时间
uint32_t nextEnvSampleMs = 0; // 下次环境采样时间

// 活动类型与文字映射
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

// 采样速率（活动自适应）
struct Rate{ uint32_t gasTempMs; uint32_t imuHz; };
Rate rateStill{15000,25};
Rate rateWalk{1500,50};
Rate rateRun{800,50};
Rate rateBike{1000,50};
Rate rateVehicle{1000,50};
Rate rateVehicleStill{1000,50};
Rate rateTilt{1500,50};   

// 活动状态去抖
Activity curAct=ACT_STILL, pendingAct=ACT_STILL;
uint32_t actStableUntilMs=0;
static const uint32_t ENTER_DEBOUNCE_MS=3000; // 进入活动的去抖时间 
static const uint32_t EXIT_HOLD_MS=30000; // 结束活动后保持时间

// 预触发环形缓冲：在由静止->
struct SampleRow { uint32_t ms; Activity act; float gas,tempC,ax,ay,az,a_rms; };
constexpr size_t RING_N=8; 
SampleRow ring[RING_N]; 
size_t ringHead=0; 
bool ringFilled=false;

// 当前日志段信息
char          curSegPath[48]        = {0};       // 当前段完整路径 "logs/log_xxxxx.csv"
uint32_t      curSegIdx             = 0;         // 当前段编号
size_t        curSegBytes           = 0;         // 当前段已写入字节数（含表头）

// ========================前置声明=================================
// —— BLE 事件/工具
static void onBleConnected(BLEDevice central);
static void onBleDisconnected(BLEDevice central);
static void onBleRxWritten(BLEDevice central, BLECharacteristic ch);
static bool bleNotifyBytes(const uint8_t* data, size_t len);
static bool bleNotifyLine(const char* s);
static void listLogsOverBle();
static void bleSendFile(const char* filename);

// —— 文件系统/日志
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

// —— 活动识别/兜底
static void switchToPending(Activity a, uint32_t now);
static void onActivityEvent(Activity a, bool started);
static void maybeApplyPending(uint32_t now);
static void fallbackMotionUpdate(float a_mag, uint32_t now);
static void handleRawActivityEvent(uint8_t code);
static uint8_t decodeArCode(const String& s);
static void pumpArEventIfAny();

// —— 模式切换
static inline void enterTransferMode();
static inline void leaveTransferMode();

// ===================== 工具：环形缓冲 ==========================
static void ringPush(const SampleRow& s){ 
  ring[ringHead]=s; 
  ringHead=(ringHead+1)%RING_N; 
  if(ringHead==0) ringFilled=true; 
}

// ====================== 模式切换 =============================== 
static inline void enterTransferMode(){
  g_mode = MODE_TRANSFER;
  g_transferEnterMs = millis();
  // 进入传输模式：暂停记录（把下一次采样推迟很久，且不触发 GC）
  nextEnvSampleMs = UINT32_MAX;
}

static inline void leaveTransferMode(){
  g_mode = MODE_LOGGING;
  //退出传输模式：延迟0.5s恢复采样
  nextEnvSampleMs = millis() + 500; 
}


// ====================== 文件系统：容量查询/目录/段操作 ===========================================
// 工具：统计剩余空间（字节）
static uint64_t fs_free_bytes(){
  struct statvfs st{};
  fs.statvfs("/", &st);
  return (uint64_t)st.f_bfree * (uint64_t)st.f_bsize;
}

static void ensureLogDir(){
  // 若目录存在则返回
  mbed::Dir d;
  if (d.open(&fs, LOG_DIR) == 0) { d.close(); return; }
  // 创建目录（-EEXIST 视为成功）
  int mk = fs.mkdir(LOG_DIR, 0777);
  if (mk != 0 && mk != -EEXIST){
    Serial.print("mkdir "); Serial.print(LOG_DIR);
    Serial.print(" failed: "); Serial.println(mk);
  }
}

static void makeSegPath(uint32_t idx){
  snprintf(curSegPath, sizeof(curSegPath), "%s/log_%05lu.csv", LOG_DIR, (unsigned long)idx);
}

// 工具：扫描 logs 目录，找到现有最小/最大段号
static bool scanMinMaxIdx(uint32_t& minIdx, uint32_t& maxIdx, uint32_t& count){
  minIdx = 0xFFFFFFFFu; maxIdx = 0; count = 0;

  mbed::Dir dir;
  if (dir.open(&fs, LOG_DIR) != 0){
    // 目录不存在或无法打开 => 视为没有段文件
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


// 工具：读取文件大小
static size_t getFileSize(const char* path){
  mbed::File f; 
  if (f.open(&fs, path, O_RDONLY)) return 0;
  size_t sz = f.size(); 
  f.close(); 
  return sz;
}

// 在当前段写入 CSV 表头（若为空）
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

// 轮转到新段
static void rotateSegment(){
  curSegIdx += 1;
  makeSegPath(curSegIdx);
  curSegBytes = 0;
  ensureSegHeader();
  Serial.print("Rotated to "); Serial.println(curSegPath);
}

// 删除最旧段（返回是否删除了某个段）
bool deleteOldestSegment(){
  uint32_t minIdx, maxIdx, count;
  if (!scanMinMaxIdx(minIdx, maxIdx, count)) return false;
  if (count==0) return false; // 目录为空，直接返回false
  if (minIdx == curSegIdx){ // 若最旧段就是当前写入的段
    if (count == 1) return false; // 当前只存在这一个段，不能删除，返回false
    minIdx++; // 否则，指向第二旧的段，即删除“下一个最旧”，保护当前写入不被删除
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/log_%05lu.csv", LOG_DIR, (unsigned long)minIdx);
  int r = fs.remove(path);
  if (r==0){ Serial.print("GC removed "); Serial.println(path); return true; }
  else { Serial.print("GC remove failed "); Serial.println(path); return false; }
}


// 检查空间并 GC；仅在“未连接中央设备”时允许执行
static void checkSpaceAndGC(){
  if (g_centralConnected) return; // 连接期间禁止 GC，避免与 BLE 抢资源

  uint64_t freeB = fs_free_bytes();
  if (freeB >= GC_MIN_FREE_BYTES) return;

  Serial.print("Low space: "); Serial.println((unsigned long)freeB);

  if (curSegBytes > (SEG_MAX_BYTES/2)){
    rotateSegment();
  }
  uint8_t guard = 32; // 防止极端情况下无限循环
  while (fs_free_bytes() < GC_TARGET_FREE_BYTES && guard--){
    if (!deleteOldestSegment()){
      Serial.println("GC cannot free more (only current segment left?).");
      break;
    }
  }
}


// 追加一行到当前段（带“空间检查 + 轮转”）
static void appendCSV(const SampleRow& s){
  // 1) 空间不足时先 GC
  checkSpaceAndGC();

  // 2) 当前段超上限？轮转
  if (curSegBytes >= SEG_MAX_BYTES){
    rotateSegment();
  }

  // 3) 再次确认仍有足够空间（极端情况下）
  if (fs_free_bytes() < 1024){ // 实在没空间就放弃写入，避免损坏
    Serial.println("FLASH FULL — skip append");
    return;
  }

  // 4) 追加写入
  mbed::File f;
  if (f.open(&fs, curSegPath, O_WRONLY | O_CREAT | O_APPEND)){
    Serial.println("open append failed"); return;
  }
  char buf[160];
  int n = snprintf(buf, sizeof(buf), "%lu,%s,%.3f,%.2f,%.3f,%.3f,%.3f,%.3f\r\n",
                   (unsigned long)s.ms, activityToStr(s.act),
                   s.gas, s.tempC, s.ax, s.ay, s.az, s.a_rms);
  static uint32_t lineCount = 0;  // 仅统计 appendCSV 写入的行（不含环形缓冲 dump）
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


// 预触发落盘
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

// 容量打印
static void printStats(){
  struct statvfs st{}; fs.statvfs("/", &st);   // 对挂载在 fs 对象上的根使用 "/"
  auto b=st.f_bsize;
  Serial.print("Total: "); Serial.println(st.f_blocks*b);
  Serial.print("Free : "); Serial.println(st.f_bfree*b);
  Serial.print("Used : "); Serial.println((st.f_blocks-st.f_bfree)*b);
}


// ========================= 活动识别：事件/去除/兜底 ===========================
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

static void fallbackMotionUpdate(float a_mag, uint32_t now){
  float dev = fabs(a_mag - A_MAG_G);
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

// 把 activityAR.getActivity() 返回的字符串解析为 0..14 的事件码；255 表示无效/未知
static uint8_t decodeArCode(const String& s) {
  if (s.length() == 0) return 255;

  // 先判断 started/ended
  bool isStarted = (s.indexOf("started") >= 0) || (s.indexOf("Started") >= 0);
  bool isEnded   = (s.indexOf("ended")   >= 0) || (s.indexOf("Ended")   >= 0);

  // 先匹配更长的 "In vehicle still"
  if (s.indexOf("In vehicle still") >= 0 || s.indexOf("IN vehicle still") >= 0) {
    return isStarted ? 14 : (isEnded ? 6 : 255);
  }
  // 其它活动关键字
  if (s.indexOf("Still") >= 0 && s.indexOf("vehicle") < 0) {
    return isStarted ? 8 : (isEnded ? 0 : 255);
  }
  if (s.indexOf("Walking") >= 0) {
    return isStarted ? 9 : (isEnded ? 1 : 255);
  }
  if (s.indexOf("Running") >= 0) {
    return isStarted ? 10 : (isEnded ? 2 : 255);
  }
  if (s.indexOf("On bicycle") >= 0) {
    return isStarted ? 11 : (isEnded ? 3 : 255);
  }
  if (s.indexOf("In vehicle") >= 0 || s.indexOf("IN vehicle") >= 0) {
    return isStarted ? 12 : (isEnded ? 4 : 255);
  }
  if (s.indexOf("Tilting") >= 0) {
    return isStarted ? 13 : (isEnded ? 5 : 255);
  }
  return 255;
}

// 处理一次 AR 事件:去重后再分发到状态机
void pumpArEventIfAny() {
  String s = activityAR.getActivity();     // 库返回“... started/ended”的字符串
  static uint8_t lastCode = 255;           // 去重，避免重复触发
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



// ========================= BLE：工具与回调 ===============================
static bool bleNotifyBytes(const uint8_t* data, size_t len){
  while (len > 0) {
    size_t n = len > BLE_CHUNK ? BLE_CHUNK : len;
    if (!nusTx.writeValue(data, n)) return false; // 分片通知
    data += n;
    len  -= n;
    delay(2); // 轻节流，避免堆栈积压；按吞吐可调
  }
  return true;
}

static bool bleNotifyLine(const char* s){
  return bleNotifyBytes((const uint8_t*)s, strlen(s));
}

// 列出 /logs 目录（统一以"F <文件名> <字节数>\n" 形式输出，末尾"F END\n"）
static void listLogsOverBle(){
  mbed::Dir dir;
  if (dir.open(&fs, LOG_DIR) != 0) {
    bleNotifyLine("#ERROR open_dir\n");
    return;
  }
  dirent ent;
  while (dir.read(&ent) > 0){
    // 只匹配 log_*.csv
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
  bleNotifyLine("F END\n");  // 结束标记
}

// 发送指定文件（filename 仅文件名，不含路径）
static void bleSendFile(const char* filename){
  printStats(); // 文件发送前打印板子空间状态
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", LOG_DIR, filename);
  Serial.print("[BleSendFile] 打开文件: "); Serial.print(path);

  mbed::File f;
  if (f.open(&fs, path, O_RDONLY) != 0) { 
    Serial.print("[BleSendFile] 打开文件失败");
    bleNotifyLine("#ERROR open\n");
    return;
  }

  // BEGIN 标记（含文件大小，便于客户端先建文件）
  size_t fsz = f.size();
  char head[128];
  snprintf(head, sizeof(head), "#BEGIN %s %lu\n", filename, (unsigned long)fsz);
  bleNotifyLine(head);

  // 按 CHUNK 读取并通知
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
  printStats();  // 文件发送后打印板子空间状态

  bleNotifyLine("\n#END ");
  bleNotifyLine(filename);
  bleNotifyLine("\n");
}

// 连接/断开回调：在连接时进入传输模式，断开时回到记录模式
static void onBleConnected(BLEDevice central){
  printStats();
  g_centralConnected = true;
  enterTransferMode();            // 一连上就切换到传输模式
  Serial.println("[BLE] connected -> TRANSFER");
}
static void onBleDisconnected(BLEDevice central){
  g_centralConnected = false;
  leaveTransferMode();            // 断开回到记录模式
  Serial.println("[BLE] disconnected -> LOGGING");
}

// RX 写入事件：解析命令（List/GET<name>/ABORT）
void onBleRxWritten(BLEDevice central, BLECharacteristic ch){
  int len = ch.valueLength();
  if (len <= 0) return;

  static char cmd[160];
  len = min(len, (int)sizeof(cmd)-1);
  memcpy(cmd, ch.value(), len);
  cmd[len] = 0;

  // 去掉结尾 \r\n
  while (len>0 && (cmd[len-1]=='\n' || cmd[len-1]=='\r')) cmd[--len]=0;

  Serial.print("[BLE RX] "); Serial.println(cmd);

  if (strncmp(cmd, "LIST", 4) == 0) {
    listLogsOverBle();
  } else if (strncmp(cmd, "GET ", 4) == 0) {
    const char* name = cmd + 4;
    while (*name==' ') name++;

    if (*name == 0) {
      bleNotifyLine("#ERROR no_filename\n");
    } else {
      bleSendFile(name);
    }
  } else if (strncmp(cmd, "ABORT", 5) == 0) {
    bleAbort = true;
  } else {
    bleNotifyLine("#ERROR unknown_cmd\n");
  }
}

// =========================== setup / loop =============================
void setup(){
  Serial.begin(115200);
  for (auto t=millis()+2500; !Serial && millis()<t; delay(100)) {}

  // ---- 1) 初始化 BLE：建服务、特征、回调、开始广播 ----
  if (!BLE.begin()) {
    Serial.println("BLE begin failed!");
  } else {
    // 连接区间（单位 1.25ms）: 12*1.25=15ms, 24*1.25=30ms(兼顾功耗与兼容性)
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

  delay(200); // 给 BLE 子系统一点稳定时间（经验做法）

  // ---- 2) LittleFS：挂载&准备分段文件 ----
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

  // ---- 3) 传感器：最后初始化，避免与 BLE 初始化并发 ----
  Serial.print("Init sensors...");
  BHY2.begin(NICLA_STANDALONE);
  accel.begin(); temp.begin(); gas.begin();
  activityAR.begin();
  Serial.println(" done.");

  nextEnvSampleMs   = millis() + 500;
  actStableUntilMs  = UINT32_MAX;
  lastArEventMs     = millis();

  g_mode = MODE_LOGGING;   // 初始处于记录模式
}

void loop(){
  BLE.poll();            // 任何模式都要处理 BLE 事件
  uint32_t now = millis();

  // ---- 传输模式：仅处理BLE（文件操作通过onBleRxWritten的命令驱动）----
  if (g_mode == MODE_TRANSFER){
    // 刚进入传输模式的前 1.5s 内，不做任何 FS 操作，给中央完成服务发现
    if ((int32_t)(now - g_transferEnterMs) < TRANSFER_SILENCE_MS) {
      return;
    }
    // 传输模式下，不跑传感器、不写文件、不做 GC；所有动作通过 onBleRxWritten() 的 LIST/GET/ABORT 处理。
    return;
  }

  // ---- 记录模式：更新传感器FIFO、消费AR事件、按节奏采样并落盘----
  BHY2.update();  // 喂传感器 FIFO，让 AR/加速度数据可用
  pumpArEventIfAny();  // 消费一次AR事件（若有）

  // 若 AR 静默，用加速度兜底判断运动状态
  if ((int32_t)(now - lastArEventMs) > (int32_t)AR_SILENCE_MS) {
    float ax=accel.x(), ay=accel.y(), az=accel.z();
    float a_mag = sqrtf(ax*ax + ay*ay + az*az);
    fallbackMotionUpdate(a_mag, now);
  }

  maybeApplyPending(now);  // 去抖/保持 & 触发 pre-roll 落盘

  // 活动自适应的采样周期（IMU频率参数暂保留）
  Rate r =
    (curAct==ACT_STILL)         ? rateStill :
    (curAct==ACT_WALK)          ? rateWalk  :
    (curAct==ACT_RUN)           ? rateRun   :
    (curAct==ACT_BIKE)          ? rateBike  :
    (curAct==ACT_VEHICLE)       ? rateVehicle :
    (curAct==ACT_VEHICLE_STILL) ? rateVehicleStill :
                                  rateTilt;

  if ((int32_t)(now - nextEnvSampleMs) >= 0){
    float ax=accel.x(), ay=accel.y(), az=accel.z();
    float a_rms = sqrtf((ax*ax + ay*ay + az*az)/3.0f);
    float gasv=gas.value();
    float tempC=temp.value();

    SampleRow row{now,curAct,gasv,tempC,ax,ay,az,a_rms};
    ringPush(row);
    appendCSV(row);

    // 写盘后再喂一次传感器FIFO, 避免长写导致的数据堆积
    BHY2.update();

    nextEnvSampleMs = now + r.gasTempMs;
  }
}