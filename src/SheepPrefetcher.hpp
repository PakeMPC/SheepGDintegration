#pragma once
#include <Geode/Geode.hpp>
#include "QueueData.hpp"
#include "SheepSongs.hpp"
#include "SheepI18n.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

// Precargador de niveles: descarga por adelantado los niveles que estan a punto de
// jugarse (el siguiente de la cola y el actual), para dos cosas:
//
// - Leer sus listas reales de audio (GJGameLevel::m_songIDs y m_sfxIDs) y pedir
//   todo lo que falte. Es la unica forma: el JSON solo trae CustomSongId, y los
//   SFX van dentro de los objetos del nivel, asi que no se conocen hasta que el
//   nivel esta descargado (la busqueda no los trae).
// - Que el salto de nivel sea inmediato: SheepLevelLauncher le pide el nivel con
//   takeLevel y, si ya lo tiene, se ahorra la busqueda y la descarga.
//
// Lo mueve SheepData en cada tick (0,5 s), con update().
//
// Es un singleton retenido, como SheepData, pero por la razon contraria: este
// objeto espera respuestas de red que pueden no llegar nunca, porque el hueco del
// delegado de GameLevelManager es unico y una accion del usuario se lo puede
// quitar a mitad de descarga. Un objeto por peticion se quedaria colgado para
// siempre o, peor, se liberaria dejando m_levelDownloadDelegate apuntando a
// memoria liberada.
//
// Reglas del hueco del delegado, que comparte con la escena, el lanzador y el
// buscador del placeholder: como tarea de segundo plano, solo lo ocupa si esta
// libre; se aparta cuando se lo piden (yield); y solo actua si la respuesta trae
// el nivel que espera.
//
// Un intento solo se cobra si se llego a intentar y fallo: ceder el hueco no
// cuenta. Y los intentos caducan, para que un nivel que agoto los suyos con la red
// caida vuelva a precargarse cuando la red vuelve.
class SheepPrefetcher : public CCObject,
                        public LevelManagerDelegate,
                        public LevelDownloadDelegate
{
public:
    // La instancia unica. Se crea la primera vez que se pide.
    static SheepPrefetcher* get() {
        static SheepPrefetcher* inst = [] {
            auto p = new SheepPrefetcher();
            p->retain();          // nunca se libera: dura lo que dura el juego
            return p;
        }();
        return inst;
    }

    // Un paso por tick, a partir de los datos actuales de la cola. Nunca bloquea.
    void update(QueueData const& data) {
        ++m_tick;
        watchdog();

        // Objetivos, por urgencia: el siguiente de la cola (es al que salta el
        // auto-skip) y el actual (por si se pulsa Play en la tarjeta).
        std::vector<int> targets;
        if (!data.queue.empty()) {
            int id = data.queue[0].levelIdAsInt();
            if (id > 0) targets.push_back(id);
        }
        if (data.current.isValid()) {
            int id = data.current.levelIdAsInt();
            if (id > 0 && std::find(targets.begin(), targets.end(), id) == targets.end())
                targets.push_back(id);
        }

        // Los niveles que ya no son objetivo se sueltan.
        forgetOutside(targets);

        // Empujar el audio de los niveles que ya se tienen. En cada tick y sin
        // condicion de parada: ensureSong y ensureSfx avanzan un paso por llamada,
        // y levelAudioReady no sirve aqui como condicion porque ignora los SFX;
        // parar al cumplirse dejaba los efectos a medias.
        for (int id : targets) {
            auto it = m_held.find(id);
            if (it != m_held.end() && it->second)
                sheep::ensureLevelAudio(it->second, fallbackFor(id, data));
        }

        if (m_pendingId) return;   // uno en curso: de uno en uno, nunca dos

        // Empezar la descarga del primer objetivo que falte y se pueda intentar.
        for (int id : targets) {
            if (m_held.count(id)) continue;
            if (!canTry(id)) continue;
            startFetch(id);
            return;
        }
    }

    // Entrega el nivel ya descargado, si se tiene; nullptr si no.
    //
    // No suelta su propia referencia: quien lo reciba debe retenerlo por su cuenta,
    // y cada uno suelta la suya cuando termina.
    GJGameLevel* takeLevel(int levelId) {
        auto it = m_held.find(levelId);
        return (it != m_held.end()) ? it->second : nullptr;
    }

    // Se aparta del hueco del delegado. Lo llama quien lo va a ocupar por una
    // accion de primer plano (SheepLevelLauncher::play): el segundo plano siempre
    // cede. Lo que estuviera en curso se da por perdido, y el siguiente tick vuelve
    // a empezar. Sin esto la unica salida era el watchdog, es decir, 20 segundos
    // parado.
    void yield() {
        if (!m_pendingId) return;

        log::debug("[SheepGD] {}", sheep::trf("Log_PrefetchYield", m_pendingId));
        detachManager();
        detachDownload();

        // El intento no cuenta: no ha fallado nada, se ha cedido el hueco. Cobrarlo
        // gastaria el presupuesto de reintentos de ese nivel sin motivo.
        refundTry();

        clearPending();
    }

    // --- LevelManagerDelegate ---------------------------------------------

    // Resultado de la busqueda: si esta el nivel que se buscaba, se pide su
    // descarga.
    void loadLevelsFinished(cocos2d::CCArray* levels, char const*) override {
        if (!m_pendingId) return;
        auto found = findInArray(levels, m_pendingId);
        detachManager();
        if (!found) { clearPending(); return; }
        beginDownload(found);
    }
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int) override {
        loadLevelsFinished(levels, key);
    }

    void loadLevelsFailed(char const*) override { detachManager(); clearPending(); }
    void loadLevelsFailed(char const* key, int) override { loadLevelsFailed(key); }

    // --- LevelDownloadDelegate --------------------------------------------

    // Descarga terminada: si es la propia, el nivel se guarda (hold).
    void levelDownloadFinished(GJGameLevel* level) override {
        // El hueco es unico y compartido: si lo que llega no es lo propio, no se
        // hace nada y se sigue registrado. Soltar el delegado aqui seria darse de
        // baja de la propia descarga.
        if (!level || level->m_levelID != m_pendingId) return;

        detachDownload();
        hold(level);
        clearPending();
        // .value(): m_levelID es un SeedValueRSV, no un int. log::debug lo aceptaba
        // porque Geode sabe formatear los tipos de sus bindings, pero trf llama a
        // fmt::format directamente y fmt no los conoce.
        log::debug("[SheepGD] {}", sheep::trf("Log_PrefetchDone", level->m_levelID.value()));
    }

    void levelDownloadFailed(int) override {
        // Lo que llega es el codigo de error, no el id del nivel, asi que aqui no hay
        // forma de saber si el fallo era propio. Por eso no se suelta nada: de eso se
        // encarga el watchdog, que si sabe cuanto lleva esperando lo suyo.
    }

private:
    SheepPrefetcher() = default;

    // Intentos gastados con un nivel y tick del ultimo intento.
    struct Try { int tries = 0; int lastTick = -10000; };

    static constexpr int kMaxLevelTries   = 4;
    static constexpr int kLevelRetryTicks = 20;   // 20 x 0,5 s = 10 s
    static constexpr int kPendingTimeout  = 40;   // 20 s sin respuesta -> se abandona
    static constexpr int kTriesResetTicks = 120;  // 60 s despues del ultimo, de cero

    // Si nadie contesta en kPendingTimeout ticks, el intento se da por perdido.
    // Cubre las dos formas de quedarse colgado: que la red no responda, y que una
    // accion del usuario se haya quedado el hueco del delegado y la respuesta
    // propia le haya llegado a otro.
    void watchdog() {
        if (!m_pendingId) return;
        if (m_tick - m_pendingSince < kPendingTimeout) return;

        log::debug("[SheepGD] {}", sheep::trf("Log_PrefetchTimeout", m_pendingId));
        detachManager();
        detachDownload();
        clearPending();
    }

    // Indica si se puede intentar ese nivel ahora: quedan intentos y ya paso el
    // tiempo de espera desde el ultimo. Con los intentos agotados, pasados
    // kTriesResetTicks desde el ultimo se empieza de cero.
    bool canTry(int levelId) {
        auto& t = m_tries[levelId];
        if (t.tries >= kMaxLevelTries) {
            if (m_tick - t.lastTick < kTriesResetTicks) return false;
            t = Try{};
        }
        if (m_tick - t.lastTick < kLevelRetryTicks) return false;
        return true;
    }

    // Empieza la busqueda de un nivel (o directamente la descarga, si la busqueda
    // ya esta en la cache de GD).
    void startFetch(int levelId) {
        auto glm = GameLevelManager::sharedState();
        if (!glm) return;

        auto search = GJSearchObject::create(SearchType::Search, std::to_string(levelId));

        // Si ya se busco antes, se ahorra el viaje y se va directamente a la
        // descarga, que es la que trae m_songIDs y m_sfxIDs.
        if (auto stored = glm->getStoredOnlineLevels(search->getKey())) {
            auto& t = m_tries[levelId];
            t.lastTick = m_tick;
            ++t.tries;

            m_pendingId    = levelId;
            m_pendingSince = m_tick;
            beginDownload(findInArray(stored, levelId));
            return;
        }

        // En segundo plano el hueco solo se ocupa si esta libre. Si no, ni siquiera
        // se gasta intento: no se ha llegado a intentar.
        if (glm->m_levelManagerDelegate) return;

        auto& t = m_tries[levelId];
        t.lastTick = m_tick;
        ++t.tries;

        m_pendingId    = levelId;
        m_pendingSince = m_tick;
        glm->m_levelManagerDelegate = this;
        glm->getOnlineLevels(search);
        log::debug("[SheepGD] {}", sheep::trf("Log_PrefetchSearching", levelId, t.tries));
    }

    // Pide la descarga del nivel encontrado. Si el hueco de descargas esta ocupado,
    // cede y lo deja para otro tick, y el intento no cuenta: es la misma regla que
    // la de la busqueda con el hueco ocupado, y la de yield.
    void beginDownload(GJGameLevel* found) {
        auto glm = GameLevelManager::sharedState();
        if (!found || !glm) { clearPending(); return; }
        if (glm->m_levelDownloadDelegate) {   // ocupado: se cede
            refundTry();
            clearPending();
            return;
        }

        glm->m_levelDownloadDelegate = this;
        glm->downloadLevel(found->m_levelID, false, 0);
    }

    // Busca un nivel por id dentro de los resultados de una busqueda.
    static GJGameLevel* findInArray(cocos2d::CCArray* levels, int levelId) {
        if (!levels) return nullptr;
        for (unsigned int i = 0; i < levels->count(); ++i) {
            auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if (lvl && lvl->m_levelID == levelId) return lvl;
        }
        return nullptr;
    }

    // El CustomSongId del JSON, como red de seguridad por si el servidor no manda
    // m_songIDs (niveles antiguos, o respuestas que no los incluyen).
    static int fallbackFor(int levelId, QueueData const& data) {
        if (data.current.levelIdAsInt() == levelId) return data.current.customSongIdAsInt();
        for (auto const& lv : data.queue)
            if (lv.levelIdAsInt() == levelId) return lv.customSongIdAsInt();
        return 0;
    }

    // Guarda un nivel descargado, retenido, sustituyendo al anterior del mismo id.
    void hold(GJGameLevel* level) {
        if (!level) return;
        int id = level->m_levelID;

        auto it = m_held.find(id);
        if (it != m_held.end()) {
            if (it->second == level) return;
            if (it->second) it->second->release();
        }
        level->retain();   // el CCArray de resultados puede liberarse antes
        m_held[id] = level;
    }

    // Suelta los niveles guardados que ya no estan entre los objetivos, y olvida sus
    // intentos: si vuelven a serlo, empiezan con el contador a cero.
    void forgetOutside(std::vector<int> const& targets) {
        auto esObjetivo = [&](int id) {
            return std::find(targets.begin(), targets.end(), id) != targets.end();
        };
        for (auto it = m_held.begin(); it != m_held.end(); ) {
            if (esObjetivo(it->first)) { ++it; continue; }
            if (it->second) it->second->release();
            it = m_held.erase(it);
        }
        for (auto it = m_tries.begin(); it != m_tries.end(); ) {
            // El que esta en curso se queda: su intento sigue contando.
            if (esObjetivo(it->first) || it->first == m_pendingId) ++it;
            else it = m_tries.erase(it);
        }
    }

    // Devuelve el intento del nivel en curso, cuando se cede el hueco sin fallar.
    void refundTry() {
        auto it = m_tries.find(m_pendingId);
        if (it != m_tries.end() && it->second.tries > 0) --it->second.tries;
    }

    // Sueltan cada hueco del delegado, solo si lo ocupa el precargador.
    void detachManager() {
        auto glm = GameLevelManager::sharedState();
        if (glm && glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
    }
    void detachDownload() {
        auto glm = GameLevelManager::sharedState();
        if (glm && glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    }
    // Ya no hay nada en curso.
    void clearPending() { m_pendingId = 0; m_pendingSince = m_tick; }

    int m_tick         = 0;   // ticks desde el arranque (uno cada 0,5 s)
    int m_pendingId    = 0;   // nivel en curso (busqueda o descarga); 0 si ninguno
    int m_pendingSince = 0;   // tick en que empezo, para el watchdog

    std::unordered_map<int, Try>          m_tries;   // intentos por nivel objetivo
    std::unordered_map<int, GJGameLevel*> m_held;    // niveles descargados, retenidos
};