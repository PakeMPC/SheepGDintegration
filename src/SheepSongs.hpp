#pragma once
#include <Geode/Geode.hpp>
#include "QueueData.hpp"
#include "SheepI18n.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Descarga del audio de los niveles: canciones (de Newgrounds o de la biblioteca
// de musica de 2.2) y efectos de sonido (SFX).
//
// Todo funciona por pasos y sin bloquear: ensureSong, ensureSfx y
// ensureLevelAudio avanzan las descargas un paso por llamada y estan pensadas
// para llamarse en cada tick. Las llaman SheepData (cada 0,5 s), el precargador y
// las dos esperas de musica antes de entrar a un nivel (SheepLevelLauncher y
// SheepQueueLayer). levelAudioReady dice si ya se puede entrar.
//
// Cada descarga tiene un numero de intentos y un enfriamiento entre ellos, medido
// en ticks de SheepData (detail::tickCount).
// ---------------------------------------------------------------------------
namespace sheep {

    namespace detail {

        // Un intento con enfriamiento: cuantos se llevan y en que tick fue el
        // ultimo. Lo comparten canciones y SFX. Las canciones tenian antes un int
        // sin mas, porque se confiaba en isRunningActionForSongID para saber si
        // una ya estaba en camino; con la biblioteca de 2.2 esa funcion nunca
        // responde, asi que la rafaga gastaba los 8 intentos en 7 segundos y no
        // quedaba ninguno para cuando llegase la respuesta de red.
        struct Attempt { int tries = 0; int lastTick = -10000; };

        // Intentos por id de cancion y de SFX. Agotados, caducan (ver exhausted).
        inline std::unordered_map<int, Attempt>& songAttempts() {
            static std::unordered_map<int, Attempt> m;
            return m;
        }
        inline std::unordered_map<int, Attempt>& sfxAttempts() {
            static std::unordered_map<int, Attempt> m;
            return m;
        }

        // Lo que queda del cupo de consultas al disco de este tick. Lo repone
        // SheepData::onTick.
        inline int& diskBudget() { static int n = 0; return n; }

        // El reloj de todos los enfriamientos, en ticks de 0,5 s. Lo avanza
        // SheepData::onTick, que es el unico sitio que se ejecuta exactamente una
        // vez por tick.
        //
        // Nota: tiene que avanzar fuera de cualquier sonda temporal, o al quitarla
        // se congelan todos los reintentos.
        inline int& tickCount() { static int t = 0; return t; }

        // Estado de la peticion de la URL base del contenido personalizado.
        inline Attempt& contentUrlAttempt() { static Attempt a; return a; }

        // Los ids que ya se sabe que estan en disco.
        //
        // Solo se memoriza el acierto: un archivo descargado no desaparece en
        // mitad de la sesion, asi que ese "true" es definitivo. El "false" no se
        // guarda nunca: ese si puede cambiar, y es justo lo que se esta esperando.
        inline std::unordered_set<int>& songsOnDisk() { static std::unordered_set<int> s; return s; }
        inline std::unordered_set<int>& sfxOnDisk()   { static std::unordered_set<int> s; return s; }
    }

    inline constexpr int kMaxSongTries   = 8;    // intentos por cancion (y por SFX)
    inline constexpr int kSongRetryTicks = 6;    // 6 x 0,5 s = 3 s entre intentos
    inline constexpr int kSfxRetryTicks  = 20;   // 20 x 0,5 s = 10 s
    inline constexpr int kMaxUrlTries    = 5;    // intentos de pedir la URL base
    inline constexpr int kUrlRetryTicks  = 20;

    // Los intentos agotados caducan: pasado este tiempo desde el ultimo, se empieza
    // de cero. Sin esto, si la red caia justo mientras se pedia algo, eso no se
    // volvia a pedir en toda la sesion aunque la red volviera, y con la URL base
    // agotada no se descargaba ninguna cancion de la biblioteca de 2.2.
    inline constexpr int kAttemptResetTicks = 120;   // 120 x 0,5 s = 60 s

    namespace detail {
        // Indica si 'a' ya no se puede intentar. Si agoto sus intentos hace mas de
        // kAttemptResetTicks, lo reinicia y devuelve false.
        inline bool exhausted(Attempt& a, int maxTries) {
            if (a.tries < maxTries) return false;
            if (tickCount() - a.lastTick < kAttemptResetTicks) return true;
            a = Attempt{};
            return false;
        }
    }
    // Cupo de consultas al disco por tick.
    //
    // La primera pasada de un nivel recien descargado son ~270 SFX, y a 0,55 ms cada
    // consulta (bajo Wine, ver songOnDisk) eso es un tiron de 150 ms. Repartirla en
    // varios ticks lo deja en ~20 ms, que no se nota, y el nivel acaba igual de
    // listo unos segundos despues; da igual, porque los SFX no retrasan la entrada
    // al nivel. Solo se aplica a los SFX: las canciones son pocas y esas si hacen
    // falta cuanto antes.
    inline constexpr int kDiskChecksPerTick = 40;

    // Los ids >= 10000000 son de la biblioteca de musica de 2.2, no de Newgrounds.
    // No se descargan por la URL de la ficha (ahi va el canal del artista) sino por
    // el CDN de contenido personalizado, cuya base esta en
    // MusicDownloadManager::m_customContentURL. Con ese campo vacio,
    // generateCustomContentURL devuelve una ruta sin servidor
    // ("music/10012918.ogg?token=...") y la descarga no llega a empezar.
    inline constexpr int kLibrarySongIdMin = 10000000;

    // Espera de audio al entrar a un nivel. La comparten SheepLevelLauncher y
    // SheepQueueLayer: un solo numero que cambiar cuando haya que ajustarla.
    // El sondeo es barato (un isSongDownloaded por cancion, y con memoria), asi que
    // conviene que sea fino: con 0,25 s se pagaba hasta un cuarto de segundo de
    // negro de mas solo por redondeo.
    inline constexpr float kAudioPoll    = 0.1f;
    inline constexpr float kAudioWaitMax = 5.0f;   // pasado esto se entra igual

    // Indica si la cancion esta en disco. Con memoria, y esa memoria no es un lujo.
    //
    // isSongDownloaded e isSFXDownloaded consultan el sistema de archivos, y bajo
    // Proton cada consulta pasa por la traduccion Win32 -> Unix de Wine. Medido en
    // la maquina del autor: 0,55 ms por consulta. Con 272 consultas por tick eran
    // ~150 ms de tick dos veces por segundo: unos 36 fotogramas perdidos de golpe a
    // 240 FPS, y el juego entero dando tirones.
    //
    // En un escritorio normal 272 consultas serian irrelevantes. Eso es lo que hay
    // que recordar de aqui: bajo Wine, tocar el disco cuesta tres ordenes de
    // magnitud mas de lo que uno supone.
    //
    // Devuelve true tambien para lo que no hay que descargar (canciones oficiales o
    // NONG, id <= 0) y si no se puede comprobar.
    inline bool songOnDisk(int songId) {
        if (songId <= 0) return true;                          // oficial o NONG
        if (detail::songsOnDisk().count(songId)) return true;  // ya se sabia

        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) return true;   // no se puede comprobar: no hay nada que esperar

        if (!mdm->isSongDownloaded(songId)) return false;

        detail::songsOnDisk().insert(songId);
        return true;
    }

    // Lo mismo para un SFX.
    inline bool sfxOnDisk(int sfxId) {
        if (sfxId <= 0) return true;
        if (detail::sfxOnDisk().count(sfxId)) return true;

        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) return true;
        
        if (!mdm->isSFXDownloaded(sfxId)) return false;

        detail::sfxOnDisk().insert(sfxId);
        return true;
    }

    // Pide la URL base del contenido personalizado (necesaria para la biblioteca de
    // 2.2) y deja que llegue. No bloquea: dice si ya esta lista, y quien llama
    // decide si esperar. getCustomContentURL() es de las que si tienen direccion en
    // los bindings de Windows.
    inline bool customContentReady() {
        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) return false;
        if (!mdm->m_customContentURL.empty()) return true;

        auto& a = detail::contentUrlAttempt();
        if (detail::exhausted(a, kMaxUrlTries)) return false;
        if (detail::tickCount() - a.lastTick < kUrlRetryTicks) return false;

        a.lastTick = detail::tickCount();
        ++a.tries;
        mdm->getCustomContentURL();
        log::debug("[SheepGD] {}", trf("Log_ContentUrl", a.tries));
        return false;
    }

    // Acerca una cancion un paso mas al disco. No bloquea. Esta pensada para
    // llamarse en cada tick: es una maquina de estados de tres fases, y cada llamada
    // la avanza como mucho una.
    //
    //   sin ficha  --getSongInfo-->  con ficha  --downloadSong-->  en disco
    //
    inline void ensureSong(int songId) {
        if (songId <= 0) return;   // oficial del juego o NONG: ya esta en local

        if (songOnDisk(songId)) return;   // ya esta (con memoria)

        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) return;
        if (mdm->isRunningActionForSongID(songId)) return;  // ya esta en camino

        // Fase 0, solo para la biblioteca de 2.2: la URL base. No gasta intento, a
        // proposito: la URL es una respuesta de red, y esperarla gastando intentos
        // es justo lo que rompia esto.
        if (songId >= kLibrarySongIdMin && !customContentReady()) return;

        auto& a = detail::songAttempts()[songId];
        if (detail::exhausted(a, kMaxSongTries)) return;
        if (detail::tickCount() - a.lastTick < kSongRetryTicks) return;

        a.lastTick = detail::tickCount();
        ++a.tries;

        if (mdm->getSongInfoObject(songId)) {
            mdm->downloadSong(songId);
            log::debug("[SheepGD] {}", trf("Log_SongDownloading", songId, a.tries));
        } else {
            mdm->getSongInfo(songId, true);
            log::debug("[SheepGD] {}", trf("Log_SongInfo", songId, a.tries));
        }
    }

    // Acerca un SFX un paso mas al disco. Igual que ensureSong, pero sin ficha: los
    // SFX se descargan directamente. Usa el mismo tope de intentos que las
    // canciones y consume una unidad del cupo de disco del tick.
    inline void ensureSfx(int sfxId) {
        if (sfxId <= 0) return;
        if (detail::sfxOnDisk().count(sfxId)) return;   // solo memoria, sin tocar el disco

        auto& a = detail::sfxAttempts()[sfxId];

        // El enfriamiento se mira antes que el disco, y no al reves como estaba antes.
        // Si no toca reintentar hasta dentro de 10 s, tampoco hace falta preguntar si
        // ya llego: eso divide por veinte el caso peor, que es un nivel recien puesto
        // con todos sus efectos por descargar.
        if (detail::exhausted(a, kMaxSongTries)) return;
        if (detail::tickCount() - a.lastTick < kSfxRetryTicks) return;
        if (detail::diskBudget() <= 0) return;   // se acabo el cupo, sigue en el siguiente tick
        --detail::diskBudget();
        if (sfxOnDisk(sfxId)) return;   // aqui si se consulta, y se memoriza

        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) return;

        a.lastTick = detail::tickCount();
        ++a.tries;

        mdm->downloadSFX(sfxId);
        log::debug("[SheepGD] {}", trf("Log_SfxDownloading", sfxId, a.tries));
    }

    // "1234,5678,9012" -> {1234, 5678, 9012}. Asi vienen las listas de audio dentro
    // del GJGameLevel: una cadena separada por comas, no un array. Los trozos vacios
    // o que no son un numero positivo se ignoran.
    inline std::vector<int> splitIds(std::string const& csv) {
        std::vector<int> out;
        size_t start = 0;
        while (start <= csv.size()) {
            auto end = csv.find(',', start);
            if (end == std::string::npos) end = csv.size();

            auto piece = csv.substr(start, end - start);
            if (!piece.empty()) {
                int n = geode::utils::numFromString<int>(piece).unwrapOr(0);
                if (n > 0) out.push_back(n);
            }
            if (end == csv.size()) break;
            start = end + 1;
        }
        return out;
    }

    // Pide todo el audio que necesita un nivel: sus canciones y sus SFX. Requiere un
    // GJGameLevel ya descargado: las listas vienen del servidor junto con el nivel,
    // no con la busqueda.
    //
    // fallbackSongId es el CustomSongId del JSON, por si el servidor no manda las
    // listas (niveles antiguos, o respuestas que no las incluyen).
    inline void ensureLevelAudio(GJGameLevel* level, int fallbackSongId = 0) {
        if (!level) return;

        auto songs = splitIds(std::string(level->m_songIDs.c_str()));
        if (songs.empty() && fallbackSongId > 0) songs.push_back(fallbackSongId);
        for (int id : songs) ensureSong(id);

        for (int id : splitIds(std::string(level->m_sfxIDs.c_str()))) ensureSfx(id);
    }

    // Indica si se puede entrar al nivel sin que falte la musica: todas sus
    // canciones estan en disco.
    inline bool levelAudioReady(GJGameLevel* level, int fallbackSongId = 0) {
        if (!level) return true;
        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm) return true;

        auto songs = splitIds(std::string(level->m_songIDs.c_str()));
        if (songs.empty() && fallbackSongId > 0) songs.push_back(fallbackSongId);

        for (int id : songs) {
            if (!songOnDisk(id)) return false;
        }

        // Los SFX no cuentan para la espera, a proposito. Un nivel sin su musica se
        // nota muchisimo; sin un efecto puntual, casi nada. Y son muchos mas:
        // esperarlos todos alargaria cada entrada sin ganancia real. Se piden con
        // ensureLevelAudio y llegan cuando lleguen.
        return true;
    }

    // Pide la cancion del nivel actual y de los 'lookahead' primeros de la cola. Solo
    // la cancion que trae el JSON (CustomSongId); los SFX y las canciones multiples
    // los pide el precargador, que es quien tiene los niveles descargados.
    inline void prefetchSongs(QueueData const& data, size_t lookahead = 2) {
        std::vector<int> ids;
        ids.push_back(data.current.customSongIdAsInt());
        for (size_t i = 0; i < lookahead && i < data.queue.size(); ++i) {
            ids.push_back(data.queue[i].customSongIdAsInt());
        }

        for (int id : ids) ensureSong(id);
    }

} // namespace sheep