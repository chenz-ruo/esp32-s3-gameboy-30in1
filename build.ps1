# build.ps1 - build / flash this project without the EIM activation script
# (that script aborts if an optional gdb component is not installed).
#
#   .\build.ps1 build
#   .\build.ps1 flash -Port COM4
#   .\build.ps1 monitor -Port COM4
#
# The three IDF_* paths below are the defaults for the machine this project
# was developed on; set them as environment variables to override.  The
# project path itself is always the directory containing this script, so a
# fresh clone works without editing anything here.
param(
    [string]$Action = "build",
    [string]$Port = "COM4"
)

$ErrorActionPreference = "Stop"

if (-not $env:IDF_PATH)            { $env:IDF_PATH = "C:\esp\v6.1\esp-idf" }
if (-not $env:IDF_TOOLS_PATH)      { $env:IDF_TOOLS_PATH = "C:\Espressif" }
if (-not $env:IDF_PYTHON_ENV_PATH) { $env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v6.1\venv" }
# idf_component_manager compares this with 6.0.1; without it idf.py throws
# TypeError: expected string or bytes-like object, got 'NoneType'.
if (-not $env:ESP_IDF_VERSION)     { $env:ESP_IDF_VERSION = "6.1.0" }

$toolPaths = @(
    "C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin",
    "C:\Espressif\tools\cmake\4.0.3\bin",
    "C:\Espressif\tools\ninja\1.12.1",
    "C:\Espressif\tools\idf-exe\1.0.3",
    "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64",
    "C:\Espressif\tools\python\v6.1\venv\Scripts"
)
$env:PATH = ($toolPaths -join ";") + ";" + $env:PATH

$py  = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"
$idf = Join-Path $env:IDF_PATH "tools\idf.py"
$proj = $PSScriptRoot

switch ($Action) {
    "build"   { & $py $idf -C $proj build }
    "clean"   { & $py $idf -C $proj fullclean }
    "flash"   { & $py $idf -C $proj -p $Port -b 921600 flash }
    "monitor" { & $py $idf -C $proj -p $Port monitor }
    default   { & $py $idf -C $proj $Action }
}
exit $LASTEXITCODE
