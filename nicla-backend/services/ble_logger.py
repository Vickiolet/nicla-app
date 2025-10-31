import asyncio
import os
from pathlib import Path
import contextlib
from collections import deque
from typing import Optional, List, Deque, Dict
from bleak import BleakClient, BleakScanner, BleakError
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData

# ==========================
# 固件/NUS相关常量
# ==========================
TARGET_NAME      = "NiclaSenseME-Logger"
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # host -> nicla (Write[/NoResp])
NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # nicla -> host (Notify)


# ==========================
# BLE数据解析状态
# ==========================
class ParserState:
    def __init__(self):
        # 当前正在写入的目标文件句柄 (.part 临时文件)
        self.current_fp: Optional[object] = None

        # 来自 "LIST\n" 的文件名集合 + 事件
        self.listed_files: List[str] = []
        self.list_done = asyncio.Event()

        # 来自 "GET <fn>\n" 的单文件完成事件
        self.file_done = asyncio.Event()

        # 行缓冲
        self.buf = bytearray()
        self.lines: Deque[bytes] = deque()

        # 最近一次完成的 #END 文件名 (用于 ACK)
        self.last_end_name: Optional[str] = None

    def reset_for_list(self):
        self.listed_files.clear()
        # Python 3.11+ asyncio.Event 有 clear()
        self.list_done.clear()

    def reset_for_file(self):
        self.file_done.clear()
        self.last_end_name = None

    def close_file(self):
        if self.current_fp:
            try:
                self.current_fp.flush()
                self.current_fp.close()
            except Exception:
                pass
            self.current_fp = None


# 把 notify 回调的原始 bytes 投递到 asyncio.Queue 里
class NotifyPipe:
    def __init__(self):
        self.q: asyncio.Queue[bytes] = asyncio.Queue(maxsize=4096)

    def cb(self, _handle: int, data: bytearray):
        try:
            self.q.put_nowait(bytes(data))
        except asyncio.QueueFull:
            # 极端背压情况下丢掉最老的并继续也可以，这里简单忽略
            pass


# 我们把"一台还保持连接的设备"的所有运行中对象都封在这里
class ConnectedDeviceState:
    def __init__(self, client, disc_evt, pipe, parser_state, parser_task):
        self.client = client               # BleakClient
        self.disc_evt = disc_evt           # asyncio.Event，当断开/异常会被置位
        self.pipe = pipe                   # NotifyPipe
        self.parser_state = parser_state   # ParserState
        self.parser_task = parser_task     # asyncio.Task 负责持续解析 notify 数据


# ==========================
# BLELogger 主类（后端单例）
# ==========================
class BLELogger:
    def __init__(self, logs_dir: str = "pulled_logs"):
        # 本地保存下来的日志文件目录
        self.logs_dir = Path(logs_dir)
        self.logs_dir.mkdir(parents=True, exist_ok=True)

        # 还保持着BLE连接/正在配置模式的设备们
        # key: device_address, value: ConnectedDeviceState
        self.connected: Dict[str, ConnectedDeviceState] = {}

    # ---------- 基础工具 ----------

    async def find_device(self, timeout: float = 12.0) -> Optional[BLEDevice]:
        """按名字匹配 TARGET_NAME 去找设备（兜底用）"""
        dev = await BleakScanner.find_device_by_filter(
            lambda d, adv: TARGET_NAME in (d.name or "") or TARGET_NAME in (adv.local_name or ""),
            timeout=timeout
        )
        return dev

    async def wait_services_ready(self, client: BleakClient, tries: int = 12, delay: float = 0.25) -> bool:
        """反复试图访问 client.services，直到不抛异常，表示 GATT 服务树准备好了"""
        for _ in range(tries):
            try:
                _ = client.services  # 触发解析
                return True
            except Exception:
                await asyncio.sleep(delay)
        return False

    async def safe_write(self, client: BleakClient, uuid: str, data: bytes, disc_evt: asyncio.Event):
        """Write With Response；用于必须可靠送达的命令（CONFIG、LIST、ACK、STARTLOG）"""
        if disc_evt.is_set():
            raise BleakError("Disconnected")
        if not client.is_connected:
            raise BleakError("Not connected")
        await client.write_gatt_char(uuid, data, response=True)

    async def fire_and_forget_write(self, client: BleakClient, uuid: str, data: bytes, disc_evt: asyncio.Event):
        """Write Without Response；用于 GET <fn>，减少阻塞"""
        if disc_evt.is_set():
            raise BleakError("Disconnected")
        if not client.is_connected:
            raise BleakError("Not connected")
        await client.write_gatt_char(uuid, data, response=False)

    def feed_lines(self, buf: bytearray, chunk: bytes, out_deque: Deque[bytes]):
        """把BLE Notify收到的原始字节流按 '\n' 切成行"""
        buf.extend(chunk)
        while True:
            idx = buf.find(b"\n")
            if idx < 0:
                break
            line = buf[:idx + 1]
            del buf[:idx + 1]
            out_deque.append(line)

    def safe_decode(self, b: bytes) -> str:
        """把一行bytes转成utf-8（退化到latin1以防乱码崩溃）"""
        try:
            return b.decode("utf-8")
        except UnicodeDecodeError:
            return b.decode("latin1", errors="replace")

    async def parser_task(self, pipe: NotifyPipe, st: ParserState):
        """
        持续消费 NotifyPipe 里的 chunk，解析协议行：
        - "#BEGIN <fn> <size>"
        - "#END <fn>"
        - "F <name> <size>" / "F END"
        - 普通数据行：写进当前打开的临时文件 .part
        这个逻辑与你现有脚本一致，只是搬进类里。:contentReference[oaicite:8]{index=8}
        """
        try:
            while True:
                chunk = await pipe.q.get()
                self.feed_lines(st.buf, chunk, st.lines)

                while st.lines:
                    line_b = st.lines.popleft()
                    line = self.safe_decode(line_b)

                    # 文件开始
                    if line.startswith("#BEGIN "):
                        parts = line.strip().split()
                        if len(parts) >= 3:
                            fn = parts[1]
                            tmp_path = self.logs_dir / (fn + ".part")
                            st.close_file()
                            st.current_fp = tmp_path.open("wb")
                        continue

                    # 文件结束
                    if line.startswith("#END "):
                        parts = line.strip().split()
                        if len(parts) >= 2:
                            end_name = parts[1]
                            st.close_file()

                            final_path = self.logs_dir / end_name
                            tmp_path = self.logs_dir / (end_name + ".part")
                            with contextlib.suppress(FileNotFoundError):
                                os.replace(tmp_path, final_path)

                            st.last_end_name = end_name
                            st.file_done.set()
                        continue

                    # LIST 列表项 or 结束
                    if line.startswith("F "):
                        s = line.strip()
                        if s == "F END":
                            st.list_done.set()
                        else:
                            # 形如 "F log_00012.csv 12345"
                            try:
                                _, name, _size = s.split()
                                st.listed_files.append(name)
                            except Exception:
                                pass
                        continue

                    # 普通数据行 => 当前文件正文
                    if st.current_fp:
                        st.current_fp.write(line_b)

        except asyncio.CancelledError:
            st.close_file()
            raise
        except Exception:
            st.close_file()
            raise

    # ---------- 设备连接 / 模式切换 ----------

    async def scan_devices(self) -> List[dict]:
        """
        主动扫描附近BLE设备，返回 {name,address} 列表。
        供 /logs/scan 使用。:contentReference[oaicite:9]{index=9}
        """
        devs = await BleakScanner.discover()
        return [
            {
                "name": d.name or "Unknown",
                "address": d.address
            }
            for d in devs
        ]

    async def connect_and_enter_config(self, device_address: str) -> dict:
        """
        1. 根据 address 连接到指定板子（或 fallback 用名字找 TARGET_NAME）
        2. 订阅通知 (NUS_TX)
        3. 起解析协程
        4. 发送 "CONFIG\n" -> 固件进入配置模式（停止采样/停止写盘）
        最后把连接对象缓存进 self.connected[address]，供之后的
        /remote-files, /pull-selected, /finalize 调用。:contentReference[oaicite:10]{index=10}
        """
        # 先确认有没有这个 address
        discovered = await BleakScanner.discover()
        dev = None
        for d in discovered:
            if d.address == device_address:
                dev = d
                break
        if dev is None:
            # 兜底：按名称搜索目标固件
            dev = await BleakScanner.find_device_by_filter(
                lambda d, adv: TARGET_NAME in (d.name or "") or TARGET_NAME in (adv.local_name or ""),
                timeout=8.0
            )
            if not dev:
                return {"success": False, "message": "未找到指定设备"}

        disc_evt = asyncio.Event()
        pipe = NotifyPipe()
        st = ParserState()

        def on_disc(_c):
            disc_evt.set()
            st.close_file()

        # 不用 async with，为了后续请求还能继续用这个连接
        client = BleakClient(dev, timeout=120.0, disconnected_callback=on_disc)
        await client.connect()
        if not client.is_connected:
            return {"success": False, "message": "连接失败"}

        ok = await self.wait_services_ready(client)
        if not ok:
            await client.disconnect()
            return {"success": False, "message": "设备服务未就绪"}

        # 订阅 TX 通知，并启动后台解析
        await client.start_notify(NUS_TX_UUID, pipe.cb)
        ptask = asyncio.create_task(self.parser_task(pipe, st))

        # 让板子进入配置模式（停止采样）
        await self.safe_write(client, NUS_RX_UUID, b"CONFIG\n", disc_evt)

        # 缓存连接
        self.connected[dev.address] = ConnectedDeviceState(
            client=client,
            disc_evt=disc_evt,
            pipe=pipe,
            parser_state=st,
            parser_task=ptask
        )

        return {
            "success": True,
            "message": "已连接并进入配置模式",
            "device_address": dev.address,
            "device_name": dev.name or "Unknown"
        }

    # ---------- 阶段1：仅列出远程文件 ----------

    async def list_remote_files(self, device_address: str) -> dict:
        """
        已经连接&在配置模式下 -> 仅发 LIST，不下载。
        返回板子上的文件名列表（典型是 log_XXXXX.csv）。
        """
        state = self.connected.get(device_address)
        if state is None:
            return {"success": False, "message": "设备尚未连接。请先调用 /connect/<addr>"}

        client   = state.client
        disc_evt = state.disc_evt
        st       = state.parser_state

        if disc_evt.is_set() or (not client.is_connected):
            return {"success": False, "message": "设备已断开"}

        st.reset_for_list()
        await self.safe_write(client, NUS_RX_UUID, b"LIST\n", disc_evt)

        try:
            await asyncio.wait_for(st.list_done.wait(), timeout=8.0)
        except asyncio.TimeoutError:
            # 板端可能没有任何日志文件，也可能还没回复
            pass

        # 这里我们不做过滤也行，但通常我们只关心 log_*.csv
        file_list = [f for f in st.listed_files if f.startswith("log_")]

        return {
            "success": True,
            "files": file_list
        }

    # ---------- 阶段2：按需 GET 选中的文件 ----------

    async def pull_selected_files(self, device_address: str, files_to_get: List[str]) -> dict:
        """
        对给定的文件名数组执行逐个 GET + 等待 #END + ACK。
        不会恢复采样，也不会断开BLE，允许多次调用。
        """
        state = self.connected.get(device_address)
        if state is None:
            return {"success": False, "message": "设备尚未连接。请先调用 /connect/<addr>"}

        client   = state.client
        disc_evt = state.disc_evt
        st       = state.parser_state

        if disc_evt.is_set() or (not client.is_connected):
            return {"success": False, "message": "设备已断开"}

        pulled_files = []

        for fn in files_to_get:
            # 防御：跳过空字符串
            if not fn:
                continue

            # 为单个文件拉取做准备
            st.reset_for_file()

            # 发 GET <fn>\n （Write Without Response）
            await self.fire_and_forget_write(
                client,
                NUS_RX_UUID,
                f"GET {fn}\n".encode("utf-8"),
                disc_evt
            )

            # 等待这个文件的 #END
            try:
                await asyncio.wait_for(st.file_done.wait(), timeout=180.0)
            except asyncio.TimeoutError:
                return {
                    "success": False,
                    "message": f"获取文件 {fn} 超时",
                    "pulled_files": pulled_files
                }

            pulled_files.append(fn)

            # 发送 ACK <filename>
            ack_name = st.last_end_name or fn
            await self.safe_write(
                client,
                NUS_RX_UUID,
                f"ACK {ack_name}\n".encode("utf-8"),
                disc_evt
            )

            # 给固件一点时间再下一轮
            await asyncio.sleep(0.1)

        return {
            "success": True,
            "message": "所选文件已拉取到后端服务器",
            "pulled_files": pulled_files
        }

    # ---------- 阶段3：恢复采样并断开 ----------

    async def finalize_and_disconnect(self, device_address: str) -> dict:
        state = self.connected.get(device_address)
        if state is None:
            return {
            "success": True,
            "message": "设备已不在连接列表，可能已断开或已恢复采样"
            }

        client   = state.client
        disc_evt = state.disc_evt

        if client.is_connected and not disc_evt.is_set():
            with contextlib.suppress(Exception):
                await self.safe_write(client, NUS_RX_UUID, b"STARTLOG\n", disc_evt)

        with contextlib.suppress(Exception):
            if client.is_connected:
                await client.stop_notify(NUS_TX_UUID)

        # cancel parser task
        state.parser_task.cancel()
        try:
            await state.parser_task
        except asyncio.CancelledError:
            # 任务已取消，按预期
            pass
        except Exception as e:
            # 其他错误仍记录
            print("parser_task ended with error:", e)

        with contextlib.suppress(Exception):
            if client.is_connected:
                await client.disconnect()
        disc_evt.set()

        # remove state
        self.connected.pop(device_address, None)

        return {
        "success": True,
        "message": "设备已恢复采样并断开连接"
        }


    # ---------- 兼容旧的一键全量拉取接口 ----------

    async def pull_logs_from_device(self, device_address: str) -> dict:
        """
        兼容老接口：直接把设备上所有 log_*.csv 全部拉下来，然后 finalize_and_disconnect
        """
        # 1. 先拿远程文件列表
        list_res = await self.list_remote_files(device_address)
        if not list_res.get("success"):
            return list_res
        files = list_res.get("files", [])
        pulled_files: List[str] = []

        if not files:
            # 也要 finalize_and_disconnect，避免一直占着连接
            fin = await self.finalize_and_disconnect(device_address)
            return {
                "success": True,
                "message": "没有文件可拉取；" + fin.get("message", ""),
                "pulled_files": pulled_files
            }

        # 2. 拉所有文件
        sel_res = await self.pull_selected_files(device_address, files)
        if not sel_res.get("success"):
            return sel_res
        pulled_files = sel_res.get("pulled_files", [])

        # 3. finalize + 断开
        fin = await self.finalize_and_disconnect(device_address)

        return {
            "success": True,
            "message": "日志拉取完成；" + fin.get("message", ""),
            "pulled_files": pulled_files
        }

    # ---------- 本地文件读取（给前端预览/下载/可视化） ----------

    def get_pulled_logs(self) -> List[str]:
        """
        列出后端本地保存的日志文件名。
        之前的实现只找 .log；我们改成 .csv，因为固件发的是 log_xxxxx.csv。:contentReference[oaicite:11]{index=11}
        """
        if not self.logs_dir.exists():
            return []
        files = [
            f for f in os.listdir(self.logs_dir)
            if f.endswith(".csv")
        ]
        return sorted(files, reverse=True)

    def read_log_file(self, filename: str) -> str:
        """
        打开指定文件并把内容以文本返回，供 API 直接吐给前端可视化。
        原逻辑保持，只是允许 .csv。
        :contentReference[oaicite:12]{index=12}
        """
        fp = self.logs_dir / filename
        if not fp.exists():
            raise FileNotFoundError(f"文件不存在: {filename}")
        # 假定 CSV/文本
        with open(fp, "r", encoding="utf-8", errors="replace") as f:
            return f.read()


# 单例
ble_logger = BLELogger()
