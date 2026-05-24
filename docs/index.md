# Project Index

Quick jump links for the main logic blocks in this project.
Display paths are shown from the repo root; link targets are relative to this file.

## Core application

- Parameters and buffers: [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L38-L61) and [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L144-L145)
- Main flow and button logic: init in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1033-L1088), loop in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1126-L1146)
- I2S DMA audio pipeline: start in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L656), processing in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L807), callbacks in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1006-L1018), diagnostics in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L926)
- Recording and playback control: clip in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L552), mic record in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L573)
- UART/ESP32 + AUD/PCM protocol: UART DMA start in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L239), UART parse in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L252), AUD service in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L515), PCM TX in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L741)
- DMA/IRQ wiring: DMA init in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1233), IRQ handlers in [NIM_Assistant_F407/Core/Src/stm32f4xx_it.c](../NIM_Assistant_F407/Core/Src/stm32f4xx_it.c#L207-L249)
- MSP init (I2S/UART): [NIM_Assistant_F407/Core/Src/stm32f4xx_hal_msp.c](../NIM_Assistant_F407/Core/Src/stm32f4xx_hal_msp.c#L89-L201)

## USB CDC

- CDC interface entry points: [NIM_Assistant_F407/USB_DEVICE/App/usbd_cdc_if.c](../NIM_Assistant_F407/USB_DEVICE/App/usbd_cdc_if.c#L152-L281)
- USB device init: [NIM_Assistant_F407/USB_DEVICE/App/usb_device.c](../NIM_Assistant_F407/USB_DEVICE/App/usb_device.c#L64)
- USB descriptors (VID/PID/Product): [NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c](../NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c#L65-L69)
- Descriptor functions: [NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c](../NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c#L258-L355)

## Python tools

- Audio TCP sender: [tools/aud1_tcp_sender.py](../tools/aud1_tcp_sender.py)
- PCM TCP receiver: [tools/pcm_tcp_receiver.py](../tools/pcm_tcp_receiver.py)
- WAV to STM32 PCM converter: [tools/wav_to_stm32_pcm.py](../tools/wav_to_stm32_pcm.py)

## ESP32 (Arduino)

- Bridge sketch: [ESP32_UART_Bridge_Test/ESP32_UART_Bridge_Test.ino](../ESP32_UART_Bridge_Test/ESP32_UART_Bridge_Test.ino)
- WiFi config example: [ESP32_UART_Bridge_Test/wifi_config.example.h](../ESP32_UART_Bridge_Test/wifi_config.example.h)
- WiFi config (local): [ESP32_UART_Bridge_Test/wifi_config.h](../ESP32_UART_Bridge_Test/wifi_config.h)
