#!/bin/bash

# Script para construir el módulo del kernel y la aplicación de usuario
set -e  # Fails fast - sale al primer error

echo "=== Building Kernel Module and User Application ==="

# Verificar que estamos en el directorio correcto
if [ ! -f "Makefile" ] && [ ! -f "src/Makefile" ]; then
    echo "Error: No se encontró Makefile en el directorio actual o en src/"
    echo "Hint: Ejecuta desde el directorio raíz del proyecto"
    exit 1
fi

# Detectar headers del kernel
echo "--- Detecting Kernel Headers ---"
if [ -d "/lib/modules/$(uname -r)/build" ]; then
    KERNEL_DIR="/lib/modules/$(uname -r)/build"
    echo "Kernel headers encontrados en: $KERNEL_DIR"
elif [ -n "$KERNEL_HEADERS" ]; then
    KERNEL_DIR="$KERNEL_HEADERS"
    echo "Usando KERNEL_HEADERS: $KERNEL_DIR"
else
    echo "Error: No se encontraron kernel headers"
    echo "Hint: Instala con: sudo apt install linux-headers-$(uname -r)"
    exit 1
fi

# Verificar herramientas de compilación
echo "--- Checking Build Tools ---"
command -v make >/dev/null 2>&1 || {
    echo "Error: 'make' no encontrado"
    echo "Hint: Instala con: sudo apt install build-essential"
    exit 1
}

command -v gcc >/dev/null 2>&1 || {
    echo "Error: 'gcc' no encontrado" 
    echo "Hint: Instala con: sudo apt install gcc"
    exit 1
}

# Construir
echo "--- Building ---"
make KERNEL_DIR="$KERNEL_DIR" all

echo "=== Build completado exitosamente ==="
echo "Archivos generados:"
find . -name "*.ko" -o -name "nxp_simtemp_cli" 2>/dev/null || echo "No se encontraron archivos de salida"

echo "=====================================" 



