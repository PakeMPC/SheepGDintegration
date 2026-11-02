# Entorno para compilar SheepGDintegration (cross-compile a Windows desde Linux).
# Detalles y trampas en CLAUDE.md §9.5. Cargarlo desde ~/.profile, no desde
# ~/.bashrc: el IDE y la terminal no leen los mismos archivos de arranque.

# --- Geode ---
export GEODE_SDK=$HOME/.local/share/Geode/sdk
export SPLAT_DIR=$HOME/.local/share/Geode/cross-tools/splat
export CMAKE_TOOLCHAIN_FILE=$HOME/.local/share/Geode/cross-tools/clang-msvc-sdk/clang-cl-msvc.cmake
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
