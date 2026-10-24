@echo off
chcp 65001 >nul
title RX-8 Cluster - Waveshare ESP32-S3 LCD 2.1 yukleyici
cd /d "%~dp0"
set "BIN=firmware\rx8_cluster_WS_LCD21_merged_0x0.bin"

echo ====================================================
echo   RX-8 Cluster Simulator  -  Waveshare ESP32-S3 LCD 2.1
echo ====================================================
echo Karti UART yazan Type-C portundan bagla, diger USB-C portunu bos birak.
echo Port otomatik bulunur. Yukleme baslamazsa: BOOT tusunu basili tut,
echo RESET'e bas-birak, sonra BOOT'u birak ve bu dosyayi tekrar calistir.
echo.

where pio >nul 2>&1
if errorlevel 1 goto try_py
echo [1] PlatformIO ile derlenip yukleniyor (ws_lcd21)...
pio run -e ws_lcd21 -t upload
if errorlevel 1 goto try_py
goto ok

:try_py
where python >nul 2>&1
if errorlevel 1 goto fail
echo [2] python -m esptool ile hazir firmware yukleniyor...
python -m pip install --quiet esptool
python -m esptool --chip esp32s3 --baud 921600 write_flash 0x0 "%BIN%"
if errorlevel 1 goto fail
goto ok

:ok
echo.
echo  YUKLEME TAMAM.  Telefonu RX8-Cluster Wi-Fi agina bagla (sifre rx8rotary)
echo  ve tarayicida http://192.168.4.1 adresini ac.
echo.
where pio >nul 2>&1
if not errorlevel 1 (
  echo Seri monitor aciliyor - kapatmak icin Ctrl+C
  pio device monitor -b 115200
)
pause
exit /b 0

:fail
echo.
echo  YUKLEME BASARISIZ. Kabloyu / portu kontrol et ya da tarayicidan yukle:
echo  https://espressif.github.io/esptool-js/  -^>  adres 0x0  -^>  %BIN%
pause
exit /b 1
