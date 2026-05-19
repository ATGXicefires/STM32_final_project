# STM32F407VET6 SWD 連不上 — 排查與解決紀錄

## 問題
使用 ST-Link V2 透過 SWD 連接 STM32F407VET6 開發板，持續出現：
```
Error: Unable to get core ID
Error: No STM32 target found!
```

---

## 排查過程

### 1. 確認 ST-Link 本身沒問題
- 電腦裝置管理員有抓到 `STM32 STLink`
- ST-Link 用另一塊 **F103C8T6** 測試，連線正常
- ✅ 排除 ST-Link 硬體故障

### 2. 確認板子有過電
```
ST-LINK FW  : V2J47S7
Voltage     : 3.30V        ← 有供電
```
- D1 電源 LED 常亮
- ✅ 排除供電問題

### 3. 嘗試各種 SWD 連線方式，全部失敗

| 方式 | 結果 |
|------|------|
| 標準 SWD | ❌ |
| Under Reset (`mode=UR`) | ❌ |
| 硬體 Reset (`reset=HWrst`) | ❌ |
| 降低頻率 (`freq=100`) | ❌ |
| 交換 SWDIO / SWCLK | ❌ |

### 4. 觀察 LED 發現晶片是活的
- **D2/D3 在閃爍** → 晶片正在執行之前燒進去的程式
- 晶片沒壞，只是 SWD 通訊失敗

### 5. BOOT0 拉高進入 Bootloader
- BOOT0 接 3V3 → 按 Reset
- **D2/D3 停止閃爍** → 確認成功進入 System Bootloader
- 但 SWD 仍然連不上 → 排除「程式佔用 SWD 腳位」的可能

### 6. 確認根因：SWD 硬體通道故障
- RDP 讀出來是 Level 0（`0xAA`），沒有被鎖
- 新程式燒進去後（CubeMX 有正確配置 PA13/PA14 為 SWD），SWD 還是不通
- **結論：這塊板子的 SWD 硬體走線有問題**（虛焊、斷線、或排針沒有真正連到晶片）

### 7. 改用 USB DFU 成功連線 🎉
F407 的 System Bootloader 支援 USB DFU 協議（透過板子自帶的 USB 孔）：

```
USB speed   : Full Speed (12MBit/s)
Product ID  : STM32  BOOTLOADER
Device ID   : 0x413
Device name : STM32F405xx/F407xx/F415xx/F417xx
Device CPU  : Cortex-M4
```

燒錄成功：
```
Memory Programming ... 30.00 KB
Download verified successfully ✅
Start operation achieved successfully ✅
```

---

## 以後怎麼燒錄

因為這塊板子的 SWD 壞了，**無法使用 CubeIDE 的 Debug/Run 按鈕燒錄**。
改用以下流程：

### 燒錄步驟

```
┌─────────────────────────────────────────────────────┐
│  1. CubeIDE 按 Ctrl+B 編譯（只編譯，不燒錄）        │
│                    ↓                                 │
│  2. 硬體操作：                                       │
│     BOOT0 接 3V3 → 按 Reset → USB 線接板子 USB 孔   │
│                    ↓                                 │
│  3. 雙擊 flash_usb.bat，按任意鍵開始燒錄             │
│                    ↓                                 │
│  4. 燒完後：BOOT0 接回 GND → 按 Reset               │
│     程式開始正常執行！                                │
└─────────────────────────────────────────────────────┘
```

### 一鍵燒錄腳本
專案資料夾裡的 [flash_usb.bat](file:///d:/University%20learning%20data/hardware_c/STM32_final_project/NIM_Assistant_F407/flash_usb.bat) 會自動：
1. 找到編譯好的 `Debug\NIM_Assistant_F407.elf`
2. 透過 USB DFU 連接晶片
3. 燒錄 + 驗證
4. 自動啟動程式

### 重要注意事項

> [!WARNING]
> 每次燒錄前**必須**手動切換 BOOT0 到 3V3 並按 Reset，燒完後接回 GND 再按 Reset。
> 沒有切 BOOT0 的話，USB DFU 不會出現。

> [!TIP]
> 如果覺得每次切 BOOT0 很煩，可以買一個**帶按鈕的 BOOT0 模組**（幾塊錢），按住按鈕 + Reset 就能進 Bootloader，放開就正常跑。

> [!NOTE]
> 如果之後換了一塊 SWD 正常的板子，可以直接回去用 CubeIDE 的 Debug/Run 燒錄，不需要這個腳本。
