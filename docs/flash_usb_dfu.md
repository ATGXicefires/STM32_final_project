# STM32F407 USB DFU Flash Guide

## Summary

這塊 STM32F407VET6 板子的 SWD 連線不可用。ST-Link 本身正常，板子也有供電並能執行既有程式；BOOT0 進入 System Bootloader 後仍無法用 SWD 連線。後續已確認 RDP 是 Level 0，因此正式結論不是 RDP Level 2，而是這塊板子的 SWD 硬體通道可能有問題。

目前穩定可用的燒錄方式是 USB DFU。

## Symptoms

使用 ST-Link V2 透過 SWD 連接 STM32F407VET6 時出現：

```text
Error: Unable to get core ID
Error: No STM32 target found!
```

已確認：

- ST-Link 在另一塊 F103C8T6 上可正常連線。
- ST-Link 讀到目標電壓約 3.30V。
- D1 power LED 常亮。
- D2/D3 會閃爍，代表 STM32 正在執行舊 firmware。
- BOOT0 接 3V3 後按 Reset，D2/D3 停止閃爍，代表可進入 System Bootloader。
- RDP 讀出為 Level 0 (`0xAA`)，不是讀保護鎖死。

## Flash Steps

1. 在 STM32CubeIDE 按 `Ctrl+B` 編譯。
2. 確認產物存在：

```text
NIM_Assistant_F407/Debug/NIM_Assistant_F407.elf
```

3. BOOT0 接 3V3。
4. 按 Reset。
5. 用 USB 線接板子自己的 USB 孔，不是 ST-Link。
6. 執行：

```bat
NIM_Assistant_F407\flash_usb.bat
```

7. 燒錄成功後，BOOT0 接回 GND。
8. 再按 Reset，讓程式從 Flash 正常啟動。

## Expected USB DFU Device

STM32CubeProgrammer CLI 成功連線時會看到類似資訊：

```text
USB speed   : Full Speed (12MBit/s)
Product ID  : STM32  BOOTLOADER
Device ID   : 0x413
Device name : STM32F405xx/F407xx/F415xx/F417xx
Device CPU  : Cortex-M4
```

成功燒錄時會看到：

```text
Download verified successfully
Start operation achieved successfully
```

## Common Failure Checks

- USB DFU 裝置沒有出現：確認 BOOT0=3V3 後有按 Reset。
- 腳本找不到 `.elf`：先回 STM32CubeIDE 按 `Ctrl+B`。
- 腳本找不到 STM32CubeProgrammer：確認安裝路徑是否符合 `flash_usb.bat` 內的 `PROGRAMMER` 設定。
- 燒完後程式沒有跑：確認 BOOT0 已接回 GND，然後再按 Reset。

## Note For Future Boards

如果之後換成 SWD 正常的 STM32F407 板子，可以改回 CubeIDE Debug/Run 燒錄。這份 DFU 流程是目前這塊板子的實務 workaround。
