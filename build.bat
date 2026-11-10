@echo off
call C:/esp/v6.1/esp-idf/export.bat >nul 2>&1
idf.py %*
