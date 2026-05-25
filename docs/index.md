# Project Index

Quick jump links for the main logic blocks in this project.
Display paths are shown from the repo root; link targets are relative to this file.

## Core application

- Parameters and buffers: [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L38-L68) and [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L155-L160)
- Main flow and button logic: init in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1050-L1106), loop and button logic in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1110-L1161)
- I2S DMA audio pipeline: start in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L664), processing in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L821-L935), callbacks in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1023-L1036), diagnostics in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L943-L1015)
- Recording and playback control: clip in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L564), mic record in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L589)
- UART/ESP32 + AUD/PCM protocol: UART DMA start in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L251), UART parse in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L264), AUD service in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L527), PCM TX in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L749)
- DMA/IRQ wiring: DMA init in [NIM_Assistant_F407/Core/Src/main.c](../NIM_Assistant_F407/Core/Src/main.c#L1252), IRQ handlers in [NIM_Assistant_F407/Core/Src/stm32f4xx_it.c](../NIM_Assistant_F407/Core/Src/stm32f4xx_it.c#L207-L249)
- MSP init (I2S/UART): [NIM_Assistant_F407/Core/Src/stm32f4xx_hal_msp.c](../NIM_Assistant_F407/Core/Src/stm32f4xx_hal_msp.c#L89-L255)

## USB CDC

- CDC interface entry points: [NIM_Assistant_F407/USB_DEVICE/App/usbd_cdc_if.c](../NIM_Assistant_F407/USB_DEVICE/App/usbd_cdc_if.c#L152-L281)
- USB device init: [NIM_Assistant_F407/USB_DEVICE/App/usb_device.c](../NIM_Assistant_F407/USB_DEVICE/App/usb_device.c#L64)
- USB descriptors (VID/PID/Product): [NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c](../NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c#L65-L69)
- Descriptor functions: [NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c](../NIM_Assistant_F407/USB_DEVICE/App/usbd_desc.c#L258-L355)

## Python tools

- Stage 9 assistant server (ASR→LLM→TTS→AUD1): [tools/assistant_server.py](../tools/assistant_server.py)
- Shared config (network, ASR, LLM, TTS settings): [tools/config.py](../tools/config.py)
- Local ASR engine (faster-whisper): [tools/asr_local.py](../tools/asr_local.py)
- NVIDIA NIM LLM client: [tools/nim_llm.py](../tools/nim_llm.py)
- GPT-SoVITS TTS client: [tools/tts_sovits.py](../tools/tts_sovits.py)
- Audio TCP sender (Stage 8): [tools/aud1_tcp_sender.py](../tools/aud1_tcp_sender.py)
- PCM TCP receiver (Stage 8): [tools/pcm_tcp_receiver.py](../tools/pcm_tcp_receiver.py)
- WAV to STM32 PCM converter: [tools/wav_to_stm32_pcm.py](../tools/wav_to_stm32_pcm.py)

## ESP32 (Arduino)

- Bridge sketch: [ESP32_UART_Bridge_Test/ESP32_UART_Bridge_Test.ino](../ESP32_UART_Bridge_Test/ESP32_UART_Bridge_Test.ino)
- WiFi config example: [ESP32_UART_Bridge_Test/wifi_config.example.h](../ESP32_UART_Bridge_Test/wifi_config.example.h)
- WiFi config (local): [ESP32_UART_Bridge_Test/wifi_config.h](../ESP32_UART_Bridge_Test/wifi_config.h)
