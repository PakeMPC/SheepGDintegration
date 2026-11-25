#pragma once
#include <Geode/Geode.hpp>
#include <asp/fs/std.hpp>
#include <filesystem>

// ---------------------------------------------------------------------------
// Puente entre las rutas de Geode y las del mod.
//
// Desde Geode 6, la API de Geode (getSaveDir, readBinary, writeBinary, pick...)
// trabaja con asp::fs::Path. El mod sigue usando std::filesystem por dentro,
// porque lo usa para cosas que asp no cubre igual (last_write_time, rename,
// directory_iterator...), asi que se convierte solo en la frontera con Geode.
// ---------------------------------------------------------------------------
namespace sheep {

    // Ruta de Geode -> ruta del mod. Vacia si la conversion falla (en Windows
    // pasa por UTF-16).
    inline std::filesystem::path stdPath(asp::fs::Path const& p) {
        return p.toStdPath().unwrapOrDefault();
    }

    // Ruta del mod -> ruta de Geode.
    inline asp::fs::Path aspPath(std::filesystem::path const& p) {
        return asp::fs::Path::fromStdPath(p);
    }

} // namespace sheep