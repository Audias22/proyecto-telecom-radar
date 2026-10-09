# Compila y ejecuta las pruebas host del colgante en Windows.
# Usa g++ o clang++ si estan en PATH; si no, el compilador de zig instalado con pip (ziglang).
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force -Path build | Out-Null

$flags = @(
  '-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
  '-I../../comun/protocolo', '-I../include', '-I../src',
  'test_logica.cpp', '-o', 'build/test_logica.exe'
)

if (Get-Command g++ -ErrorAction SilentlyContinue) {
  & g++ @flags
} elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
  & clang++ @flags
} else {
  & python -c "import ziglang" 2>$null
  if ($LASTEXITCODE -ne 0) {
    Write-Error "No hay compilador C++. Instalar con: python -m pip install ziglang==0.13.0"
  }
  & python -m ziglang c++ @flags
}
if ($LASTEXITCODE -ne 0) { Write-Error "La compilacion de las pruebas fallo" }

& ./build/test_logica.exe
exit $LASTEXITCODE
