# Entorno para compilar SheepGDintegration (cross-compile a Windows desde Linux).
# Detalles y trampas en CLAUDE.md §9.5. Cargarlo desde ~/.profile, no desde
# ~/.bashrc: el IDE y la terminal no leen los mismos archivos de arranque.

# --- Geode ---
export GEODE_SDK=$HOME/.local/share/Geode/sdk
export SPLAT_DIR=$HOME/.local/share/Geode/cross-tools/splat
# CMAKE_TOOLCHAIN_FILE NO va aqui: se pasa como opcion de CMake en el perfil de
# CLion (-DCMAKE_TOOLCHAIN_FILE=...). Exportado, lo heredaba tambien la
# configuracion del Codegen, que es un programa de Linux: CMake 4.3 toma la
# variable de entorno cuando el -DCMAKE_TOOLCHAIN_FILE= que le pasan esta vacio,
# y el Codegen intentaba compilarse con las opciones de Windows (-imsvc).
# Desde la terminal: cmake ... -DCMAKE_TOOLCHAIN_FILE="$GEODE_TOOLCHAIN"
export GEODE_TOOLCHAIN=$HOME/.local/share/Geode/cross-tools/clang-msvc-sdk/clang-cl-msvc.cmake
# Por si quedo exportado de antes en ~/.profile o en la sesion grafica.
unset CMAKE_TOOLCHAIN_FILE
export HOST_ARCH=x86_64

# Bindings fijados en un clon propio, en vez de clonar 'main' en cada
# configuracion. El commit se elige a mano dentro de esa carpeta (git checkout).
export GEODE_BINDINGS_REPO_PATH=$HOME/.local/share/Geode/bindings

# --- LLVM ---
export CC=/usr/lib/llvm-19/bin/clang-cl
export CXX=/usr/lib/llvm-19/bin/clang-cl
# CC/CXX dan UN compilador; el toolchain busca otras ocho herramientas por
# nombre, y su lista de nombres versionados llega solo hasta el 17.
export LLVM_PATH=/usr/lib/llvm-19/bin

# --- CLI de Geode ---
# Si CMake no lo ve en el PATH, setup_geode_mod falla aunque 'geode' funcione
# en la terminal. Ajustar si 'which geode' dice otra carpeta.
export PATH="$HOME/.local/bin:$PATH"
