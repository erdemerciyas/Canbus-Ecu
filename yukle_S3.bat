@echo off
chcp 65001 >nul
title RX-8 Cluster - ESP32-S3 yukleyici
cd /d "%~dp0"
set "BIN=firmware\rx8_cluster_S3_merged_0x0.bin"
set "PIO_PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
set "ESPTOOL="
if exist "%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py" set "ESPTOOL=%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py"

echo ============================================
echo   RX-8 Cluster Simulator  -  ESP32-S3 yukleme
echo ============================================
echo Port otomatik bulunur. Yukleme baslamazsa: BOOT tusunu basili tut,
echo RESET'e bas-birak, sonra BOOT'u birak ve bu dosyayi tekrar calistir.
echo.

if not exist "%PIO_PY%" goto try_pio
if not defined ESPTOOL goto try_pio
echo [1] PlatformIO esptool ile hazir firmware yukleniyor...
"%PIO_PY%" "%ESPTOOL%" --chip esp32s3 --baud 921600 --before default_reset --after hard_reset write_flash 0x0 "%BIN%"
if errorlevel 1 goto try_pio
goto ok

:try_pio
if not exist "%PIO_EXE%" goto try_py
echo [2] PlatformIO ile derlenip yukleniyor (ilk seferde paket indirir)...
"%PIO_EXE%" run -e esp32s3 -t upload
if errorlevel 1 goto fail
goto ok

:try_py
where python >nul 2>&1
if errorlevel 1 goto fail
echo [3] python -m esptool ile yukleniyor...
python -m pip install --quiet esptool
python -m esptool --chip esp32s3 --baud 921600 write_flash 0x0 "%BIN%"
if errorlevel 1 goto fail
goto ok

:ok
echo.
echo  YUKLEME TAMAM.  Wi-Fi: RX8-Cluster  (sifre rx8rotary)  -^>  http://192.168.4.1
echo.
if exist "%PIO_EXE%" (
  echo Seri monitor aciliyor - kapatmak icin Ctrl+C
  "%PIO_EXE%" device monitor -b 115200
)
pause
exit /b 0

:fail
echo.
echo  YUKLEME BASARISIZ. Kabloyu / portu kontrol et ya da tarayicidan yukle:
echo  https://espressif.github.io/esptool-js/  -^>  adres 0x0  -^>  %BIN%
pause
exit /b 1
