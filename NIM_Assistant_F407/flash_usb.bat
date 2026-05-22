@echo off
echo ============================================
echo   STM32 USB DFU Flash Tool
echo ============================================
echo.

set "PROGRAMMER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
set "ELF_FILE="

if not exist "%PROGRAMMER%" (
    set "PROGRAMMER=D:\STM32cubeIDE\STM32CubeIDE_1.18.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.100.202412061334\tools\bin\STM32_Programmer_CLI.exe"
)

if not exist "%PROGRAMMER%" (
    echo [ERROR] STM32CubeProgrammer not found!
    echo.
    pause
    exit /b 1
)

:: Auto-detect .elf file in Debug folder
for %%f in ("%~dp0Debug\*.elf") do set "ELF_FILE=%%f"

if not defined ELF_FILE (
    echo [ERROR] No .elf file found in Debug folder!
    echo         Please build in CubeIDE first [Ctrl+B]
    echo.
    pause
    exit /b 1
)

echo Found: %ELF_FILE%
echo.
echo [Step 1] Make sure:
echo   - BOOT0 connected to 3V3
echo   - USB cable plugged into board
echo   - Reset button pressed
echo.
echo Press any key to start flashing...
pause >nul

echo.
echo [Step 2] Connecting USB DFU Bootloader...
echo.
"%PROGRAMMER%" -c port=USB1 -d "%ELF_FILE%" -v -s 0x08000000

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================
    echo   Flash OK!
    echo   Now: BOOT0 back to GND, then press Reset
    echo ============================================
) else (
    echo.
    echo ============================================
    echo   Flash FAILED!
    echo   Check: BOOT0=3V3? USB? Reset pressed?
    echo ============================================
)

echo.
pause
