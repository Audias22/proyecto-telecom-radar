#!/usr/bin/env bash
# Compila y ejecuta las pruebas host de la base (Linux, macOS o Git Bash con g++/clang++).
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-$(command -v g++ || command -v clang++ || true)}"
if [ -z "$CXX" ]; then
  echo "No se encontro g++ ni clang++. Ver README.md (opcion ziglang)." >&2
  exit 2
fi
mkdir -p build
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I../include -I../src \
  test_logica.cpp ../src/ventana_actividad.cpp ../src/formato_nube.cpp ../src/cola_peticiones.cpp \
  ../src/cuerpo_http.cpp ../src/vigilante_red.cpp \
  -o build/test_logica
./build/test_logica
