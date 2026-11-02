#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include "SheepI18n.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

// Cache de miniaturas de levelthumbs.prevter.me, en dos niveles:
//   Memoria -> CCTextureCache (clave "sheepgd-thumb-<id>"), con un mapa propio
//              id -> textura para encontrarla rapido
//   Disco   -> <saveDir>/thumbs/<id>.img, para no volver a descargarla en la
//              siguiente sesion
//
// Las descargas pasan todas por requestThumb, que comparten la tarjeta y la lista:
// asi una miniatura que ya esta en camino no se pide dos veces, y una que fallo no
// se vuelve a pedir en un rato. Es un servicio de terceros, y la lista se
// reconstruye con cada cambio del JSON.
namespace sheep {

// Tope de miniaturas en disco. Al llegar a kThumbDiskMax se borran las mas
// antiguas (por fecha de escritura) hasta dejar kThumbDiskKeep.
inline constexpr int kThumbDiskMax  = 1000;
inline constexpr int kThumbDiskKeep = 900;

// Segundos antes de volver a pedir una miniatura que fallo (un 404, por ejemplo,
// que es lo normal en un nivel sin miniatura).
inline constexpr int kThumbRetrySeconds = 300;

namespace detail {
    // Miniaturas en camino: id -> callbacks a llamar cuando llegue.
    using ThumbCallback = std::function<void(CCTexture2D*)>;
    inline std::unordered_map<std::string, std::vector<ThumbCallback>>& thumbWaiters() {
        static std::unordered_map<std::string, std::vector<ThumbCallback>> m;
        return m;
    }

    // Miniaturas que fallaron: id -> cuando.
    inline std::unordered_map<std::string, std::chrono::steady_clock::time_point>& thumbsFailed() {
        static std::unordered_map<std::string, std::chrono::steady_clock::time_point> m;
        return m;
    }

    // Archivos que hay en la carpeta de disco, contados por la limpieza. -1 mientras
    // no se haya contado. Atomico porque la limpieza corre en otro hilo.
    inline std::atomic<int>& thumbDiskCount() {
        static std::atomic<int> n{-1};
        return n;
    }
    inline std::atomic<bool>& thumbCleanupRunning() {
        static std::atomic<bool> b{false};
        return b;
    }
}

// Carpeta de la cache dentro del saveDir del mod. La crea si no existe.
inline std::filesystem::path thumbCacheDir() {
    auto dir = Mod::get()->getSaveDir() / "thumbs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Cuenta las miniaturas en disco y, si hay kThumbDiskMax o mas, borra las mas
// antiguas hasta dejar kThumbDiskKeep.
//
// Corre en un hilo aparte: bajo Wine cada consulta al disco cuesta del orden de
// medio milisegundo, y con mil archivos seria un tiron de medio segundo en el
// juego. El hilo no toca nada de cocos ni escribe en el log (las traducciones no
// estan pensadas para leerse desde otro hilo): solo el disco y el contador.
inline void enforceThumbDiskLimit() {
    if (detail::thumbCleanupRunning().exchange(true)) return;   // ya hay una en marcha

    std::thread([dir = thumbCacheDir()] {
        try {
            namespace fs = std::filesystem;
            std::vector<std::pair<fs::file_time_type, fs::path>> files;

            std::error_code ec;
            fs::directory_iterator it(dir, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                if (it->path().extension() != ".img") continue;
                std::error_code tec;
                auto t = it->last_write_time(tec);
                if (!tec) files.emplace_back(t, it->path());
            }

            int count = (int)files.size();
            if (count >= kThumbDiskMax) {
                // Las mas antiguas primero, y solo se ordenan las que se van a borrar.
                auto quitar = (std::ptrdiff_t)files.size() - kThumbDiskKeep;
                std::partial_sort(files.begin(), files.begin() + quitar, files.end(),
                    [](auto const& a, auto const& b) { return a.first < b.first; });
                for (std::ptrdiff_t i = 0; i < quitar; ++i) {
                    std::error_code rec;
                    if (fs::remove(files[i].second, rec)) --count;
                }
            }
            detail::thumbDiskCount() = count;
        } catch (...) {
            // Una excepcion que saliera del hilo cerraria el juego. Si algo falla,
            // simplemente no se limpia esta vez.
        }
        detail::thumbCleanupRunning() = false;
    }).detach();
}

// Mapa levelId -> textura ya cargada. 'static' dentro de la funcion: se construye
// la primera vez que se llama y vive hasta cerrar el juego.
//
// Las texturas se retienen al entrar en el mapa (rememberThumb). La cache de cocos
// tambien las tiene, pero si el juego la vaciara, el mapa se quedaria con punteros
// a texturas liberadas.
inline std::unordered_map<std::string, CCTexture2D*>& thumbMemCache() {
    static std::unordered_map<std::string, CCTexture2D*> cache;
    return cache;
}

// Mete una textura en el mapa, retenida. Si habia otra para ese id, la suelta.
inline void rememberThumb(std::string const& levelId, CCTexture2D* tex) {
    auto& slot = thumbMemCache()[levelId];
    if (slot == tex) return;
    if (tex)  tex->retain();
    if (slot) slot->release();
    slot = tex;
}

// Bytes de la imagen (PNG o JPG) -> textura de cocos, registrada en CCTextureCache
// con la clave 'key'. nullptr si los bytes no son una imagen valida (por ejemplo,
// una pagina de error en vez de la miniatura).
inline CCTexture2D* textureFromBytes(std::string const& key, ByteVector const& bytes) {
    if (bytes.empty()) return nullptr;

    auto img = new CCImage();
    // Sin indicar formato (kFmtUnKnown): cocos detecta por la cabecera si es PNG
    // o JPG.
    if (!img->initWithImageData(const_cast<uint8_t*>(bytes.data()), (int)bytes.size())) {
        img->release();
        return nullptr;
    }

    // addUIImage retiene la textura en la cache de cocos: por eso se puede soltar
    // el CCImage justo despues sin que la textura se libere.
    auto tex = CCTextureCache::sharedTextureCache()->addUIImage(img, key.c_str());
    img->release();
    return tex;
}

// Devuelve la textura si ya esta disponible sin usar la red (en memoria o en
// disco). nullptr = hay que descargarla.
inline CCTexture2D* cachedThumb(std::string const& levelId) {
    if (levelId.empty()) return nullptr;

    // 1) Memoria
    auto& mem = thumbMemCache();
    if (auto it = mem.find(levelId); it != mem.end()) return it->second;

    // 2) Disco
    auto file = thumbCacheDir() / (levelId + ".img");
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return nullptr;

    auto res = utils::file::readBinary(file);
    if (res.isErr()) return nullptr;

    auto tex = textureFromBytes("sheepgd-thumb-" + levelId, res.unwrap());
    if (tex) rememberThumb(levelId, tex);   // se sube a memoria para la proxima vez
    return tex;
}

// Guarda en los dos niveles de la cache lo que acaba de llegar de la red, y
// devuelve la textura. nullptr si no era una imagen valida (entonces no se guarda
// nada).
//
// Cada archivo nuevo suma al contador del disco. La primera vez en la sesion no
// hay contador todavia, y se cuenta (y se limpia si hace falta) en segundo plano;
// despues, al llegar al tope, se vuelve a limpiar.
inline CCTexture2D* storeThumb(std::string const& levelId, ByteVector const& bytes) {
    auto tex = textureFromBytes("sheepgd-thumb-" + levelId, bytes);
    if (!tex) return nullptr;

    rememberThumb(levelId, tex);

    auto written = utils::file::writeBinary(thumbCacheDir() / (levelId + ".img"), bytes);
    if (written.isErr()) {
        log::warn("[SheepGD] {}", trf("Log_ThumbCacheFailed", levelId));
        return tex;
    }

    auto& count = detail::thumbDiskCount();
    if (count < 0 || ++count >= kThumbDiskMax) enforceThumbDiskLimit();

    return tex;
}

// URL de la miniatura: la del JSON (ThumbnailUrl) si la trae; si no, se construye
// la de levelthumbs.prevter.me a partir del ID.
inline std::string thumbUrl(std::string const& levelId, std::string const& jsonUrl) {
    if (!jsonUrl.empty()) return jsonUrl;
    return fmt::format("https://levelthumbs.prevter.me/thumbnail/{}", levelId);
}

// Pide a la red la miniatura de un nivel que no esta en cache, y llama a 'cb' con
// la textura cuando llega y ya esta guardada. Si falla, 'cb' no se llama.
//
// - Si ya esta en camino, no se pide otra vez: 'cb' se apunta a la misma respuesta.
// - Si fallo hace menos de kThumbRetrySeconds, no se pide (ni se llama a 'cb').
//
// 'cb' tiene que retener lo que vaya a tocar (un Ref al nodo): se guarda hasta que
// llega la respuesta, y el nodo podria haberse destruido para entonces.
inline void requestThumb(std::string const& levelId, std::string const& url,
                         detail::ThumbCallback cb) {
    if (levelId.empty()) return;

    auto& failed = detail::thumbsFailed();
    if (auto it = failed.find(levelId); it != failed.end()) {
        if (std::chrono::steady_clock::now() - it->second
                < std::chrono::seconds(kThumbRetrySeconds)) return;
        failed.erase(it);
    }

    auto [it, nueva] = detail::thumbWaiters().try_emplace(levelId);
    it->second.push_back(std::move(cb));
    if (!nueva) return;   // ya estaba en camino

    async::spawn(
        web::WebRequest().timeout(std::chrono::seconds(10)).get(url),
        [levelId](web::WebResponse res) {
            CCTexture2D* tex = nullptr;
            if (res.ok()) {
                tex = storeThumb(levelId, res.data());
            } else if (res.code() == 404) {
                // Lo normal en un nivel sin miniatura: no merece un aviso.
                log::debug("[SheepGD] {}", trf("Log_ThumbFailed", res.code(), levelId));
            } else {
                log::warn("[SheepGD] {}", trf("Log_ThumbFailed", res.code(), levelId));
            }
            if (!tex) detail::thumbsFailed()[levelId] = std::chrono::steady_clock::now();

            // Se sacan del mapa antes de llamarlas: una puede pedir otra miniatura y
            // tocar el mapa mientras se recorre. Al salir de aqui se destruyen, y con
            // ellas los Ref que retenian sus nodos.
            auto node = detail::thumbWaiters().extract(levelId);
            if (!tex || node.empty()) return;
            for (auto& f : node.mapped()) if (f) f(tex);
        }
    );
}

} // namespace sheep