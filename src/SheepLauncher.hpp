#pragma once
#include <Geode/Geode.hpp>
#include "SheepPrefetcher.hpp"
#include "SheepData.hpp"
#include "SheepQueuePopup.hpp"
#include "SheepSongs.hpp"
#include "SheepI18n.hpp"

using namespace geode::prelude;

// Busca un nivel en los servidores, lo descarga y lo lanza con un fundido. Es el
// salto de nivel que se hace desde dentro de una partida (auto-skip, teclas Q/E,
// placeholder); la escena de la lista lanza sus niveles por su cuenta.
//
// Existe porque esto hace falta desde sitios que no son una capa (el hook de
// PlayLayer), asi que no puede apoyarse en el ciclo de vida de una escena. Se crea
// con 'new', vive por su cuenta durante el viaje de red y se libera al terminar,
// pase lo que pase: exito, fallo o nivel inexistente.
//
// El recorrido, en orden:
//   play -> [busqueda -> descarga] -> levelDownloadFinished (fundido a negro)
//        -> onBlack (espera a la musica, si hace falta) -> finishJump
//        -> swapNow (nivel nuevo) -> onVeilDone (se libera)
// Si el precargador ya tiene el nivel, se salta la busqueda y la descarga.
//
// GameLevelManager tiene un solo hueco de delegado para busquedas y otro para
// descargas, y se los disputan la escena, este lanzador, el precargador y el
// buscador del placeholder. Reglas: el segundo plano solo ocupa el hueco si esta
// libre; el primer plano lo ocupa, pero avisa antes (yield); y cada uno comprueba
// que la respuesta sea suya antes de actuar.
//
// Nadie guarda un puntero al lanzador, asi que no se le puede cancelar desde
// fuera: se cancela solo. La parte de red tiene su tiempo limite (netTimeout), y
// antes de cambiar de escena se comprueba que el jugador siga en la partida
// (finishJump). Todas las esperas van en el scheduler del director, que no muere
// con la escena: asi cada camino llega a su final y el lanzador siempre se libera.
class SheepLevelLauncher :
    public CCObject,
    public LevelManagerDelegate,
    public LevelDownloadDelegate
{
public:
    // Punto de entrada: salta al nivel levelId. No hace nada con un id no valido.
    static void play(int levelId) {
        if (levelId <= 0) return;

        // 'new' deja el contador de referencias en 1, y esa referencia es la del
        // propio lanzador. No se usa autorelease aqui: el objeto se liberaria al
        // final de este frame y la respuesta de red llegaria a un puntero liberado.
        // Cada camino de salida termina con un autorelease.
        auto self = new SheepLevelLauncher();
        self->m_levelId = levelId;

        // Atajo: si el precargador ya lo tiene descargado, se saltan la busqueda y
        // la descarga y se va directamente al fundido. Para esto existe el
        // precargador: que el salto no tenga por delante dos viajes de red con la
        // pantalla en negro.
        if (auto ready = SheepPrefetcher::get()->takeLevel(levelId)) {
            log::debug("[SheepGD] {}", sheep::trf("Log_JumpPreloaded", levelId));
            self->levelDownloadFinished(ready);
            return;
        }

        // No esta precargado. El lanzador va a ocupar el hueco del delegado, asi que
        // primero avisa al precargador para que se aparte: quitarselo sin avisar
        // funciona, pero lo deja esperando una respuesta que ya no le va a llegar
        // hasta que salte su watchdog.
        SheepPrefetcher::get()->yield();

        // Tiempo limite de la parte de red. Sin el, si GameLevelManager no llega a
        // contestar, el lanzador se queda vivo como delegado para siempre, y una
        // respuesta tardia haria el salto quiza encima de otro ya pedido. Va antes
        // de la cache: si de ahi sale un fallo, fail ya lo desprograma.
        CCDirector::get()->getScheduler()->scheduleSelector(
            schedule_selector(SheepLevelLauncher::netTimeout), self, kNetTimeout, false);

        auto glm    = GameLevelManager::sharedState();
        auto search = GJSearchObject::create(SearchType::Search, std::to_string(levelId));

        // Si la busqueda ya esta en la cache de GD, se usa sin ir a la red.
        if (auto stored = glm->getStoredOnlineLevels(search->getKey())) {
            self->loadLevelsFinished(stored, "");
            return;
        }

        glm->m_levelManagerDelegate = self;
        glm->getOnlineLevels(search);
    }

    // Resultado de la busqueda: localiza el nivel y pide su descarga.
    void loadLevelsFinished(CCArray* levels, char const*) override {
        detach();

        GJGameLevel* found = nullptr;
        if (levels) {
            for (unsigned int i = 0; i < levels->count(); ++i) {
                auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
                if (lvl && lvl->m_levelID == m_levelId) { found = lvl; break; }
            }
        }
        if (!found) { fail("Fail_NotOnServers"); return; }

        auto glm = GameLevelManager::sharedState();
        glm->m_levelDownloadDelegate = this;
        m_downloading = true;
        glm->downloadLevel(found->m_levelID, false, 0);
    }

    // Variantes con tipo que declara el delegado; hacen lo mismo que las de arriba.
    void loadLevelsFinished(CCArray* levels, char const* key, int) override {
        loadLevelsFinished(levels, key);
    }

    void loadLevelsFailed(char const*) override          { fail("Fail_SearchFailed"); }
    void loadLevelsFailed(char const* key, int) override { loadLevelsFailed(key); }

    // Nivel descargado (o ya precargado): pide su audio y empieza el fundido a
    // negro sobre la partida en curso.
    void levelDownloadFinished(GJGameLevel* level) override {
        if (!level) { fail("Fail_EmptyDownload"); return; }
        if (level->m_levelID != m_levelId) return;   // no es el nuestro
        m_downloading = false;
        stopNetTimeout();   // la parte de red ha terminado
        detach();

        // Se retiene: el CCArray de resultados puede liberarse antes de que acabe el
        // fundido, y el nivel se iria con el.
        m_level = level;
        m_level->retain();

        // El CustomSongId del JSON como red de seguridad, por si el nivel no trae
        // m_songIDs. Se calcula aqui, mientras los datos siguen siendo los de este
        // salto.
        m_fallbackSongId = fallbackFor(m_levelId);

        // Se pide el audio ya. En el camino largo (un nivel que el precargador no
        // llego a tocar) nadie lo ha pedido todavia, y esperar sin haberlo pedido
        // solo consigue agotar el tiempo de espera y entrar sin musica igualmente.
        sheep::ensureLevelAudio(m_level, m_fallbackSongId);

        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) { swapNow(); return; }   // sin escena no hay donde hacer el fundido

        // La partida desde la que se pide el salto, para comprobar en finishJump que
        // el jugador sigue en ella.
        m_fromLayer = findPlayLayer();

        // Un nivel 2.2 con triggers de cancion reparte sus pistas por varios
        // canales. En vez de suponer cuantos son, se recorre m_fmodMusic, que es el
        // registro de la musica activa y sabe el canal de cada pista.
        //
        // Nota: fadeOutMusic baja el volumen y lo deja a cero, no para la musica.
        // Es seguro porque swapNow la para del todo antes de construir el nivel.
        if (auto fmod = FMODAudioEngine::sharedEngine()) {
            for (auto const& entry : fmod->m_fmodMusic) {
                fmod->fadeOutMusic(kFadeOut, entry.second.m_channelID);
            }
        }

        // Velo de salida: nace transparente sobre la partida en curso y se oscurece.
        // Mientras tanto la partida sigue corriendo, pero ya no se ve.
        auto veil = CCLayerColor::create({0, 0, 0, 0});
        veil->setContentSize(CCDirector::get()->getWinSize());
        scene->addChild(veil, 9999);

        veil->runAction(CCFadeTo::create(kFadeOut, 255));

        // El final del fundido se espera en el scheduler del director y no con un
        // CCCallFunc en el velo: si el jugador sale del nivel durante el fundido, el
        // velo muere con la escena, la llamada no llegaria nunca y el lanzador (con
        // el nivel retenido) no se liberaria.
        CCDirector::get()->getScheduler()->scheduleSelector(
            schedule_selector(SheepLevelLauncher::blackTick), this, kFadeOut, false);
    }

    // Fin del fundido a negro. Se desprograma en la primera vuelta: solo hacia
    // falta una.
    void blackTick(float) {
        stopBlackWait();
        onBlack();
    }

    // Pantalla ya en negro y nivel todavia sin construir. Es el unico punto del
    // salto donde se puede esperar sin que se note: el espectador esta viendo un
    // fundido, y unos segundos mas de negro no cambian lo que ve; entrar sin musica
    // si se nota.
    void onBlack() {
        if (!m_level) { this->autorelease(); return; }

        // El caso normal con el precargador por delante: todo listo, sin espera.
        if (sheep::levelAudioReady(m_level, m_fallbackSongId)) { finishJump(); return; }

        log::debug("[SheepGD] {}", sheep::trf("Log_WaitingMusic", m_levelId, sheep::kAudioWaitMax));
        m_waited = 0.f;

        // En el scheduler del director y no en el de un nodo: el lanzador no es un
        // CCNode, y la escena de la que colgaria esta a punto de desaparecer.
        CCDirector::get()->getScheduler()->scheduleSelector(
        schedule_selector(SheepLevelLauncher::audioTick), this, sheep::kAudioPoll, false);
    }

    // Sondeo de la espera de musica, cada kAudioPoll segundos y como mucho
    // kAudioWaitMax. Solo cuentan las canciones: los efectos de sonido se piden
    // igual, pero no se esperan.
    void audioTick(float dt) {
        if (!m_level) { stopAudioWait(); this->autorelease(); return; }

        m_waited += dt;

        // Se empuja en cada vuelta: ensureSong y ensureSfx avanzan un paso por
        // llamada. Cuando ya no queda nada que hacer, la llamada es barata.
        sheep::ensureLevelAudio(m_level, m_fallbackSongId);

        bool ready = sheep::levelAudioReady(m_level, m_fallbackSongId);
        if (!ready && m_waited < sheep::kAudioWaitMax) return;

        stopAudioWait();
        if (ready) log::debug("[SheepGD] {}", sheep::trf("Log_MusicReady", m_levelId, m_waited));
        else       log::warn("[SheepGD] {}", sheep::trf("Log_MusicTimeout", m_levelId));

        finishJump();
    }

    // Desprograma el sondeo de audio. El scheduler retiene el objetivo, asi que
    // salir sin desprogramar seria una fuga: todos los caminos de salida pasan por
    // aqui.
    void stopAudioWait() {
        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(SheepLevelLauncher::audioTick), this);
    }

    // Lo mismo para el tiempo limite de red y para la espera del fundido.
    void stopNetTimeout() {
        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(SheepLevelLauncher::netTimeout), this);
    }
    void stopBlackWait() {
        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(SheepLevelLauncher::blackTick), this);
    }

    // La red no contesto a tiempo.
    void netTimeout(float) { fail("Fail_Timeout"); }

    // Ultima comprobacion antes de cambiar de escena: que el jugador siga en la
    // partida desde la que se pidio el salto. Si salio a la lista durante el
    // fundido o la espera de musica, el billete de vuelta ya no esta (lo consumio
    // onQuit), y hacer el cambio lo meteria en un nivel desde la lista, sin vigia
    // y sin vuelta a la escena.
    void finishJump() {
        if (!SheepQueueLayer::s_returnTicket || findPlayLayer() != m_fromLayer) {
            fail("Fail_LeftLevel");
            return;
        }
        swapNow();
    }

    // Construye el nivel nuevo y lo pone en pantalla. Se llama con la pantalla ya en
    // negro, y por eso todo lo costoso va aqui dentro.
    void swapNow() {
        if (!m_level) { this->autorelease(); return; }

        if (auto fmod = FMODAudioEngine::sharedEngine()) {
            // 1. Por la via de GD: para la musica y limpia su registro, para que el
            //    PlayLayer nuevo no herede estado del anterior.
            fmod->stopAllMusic(true);

            // 2. Red de seguridad por debajo de GD. El grupo maestro de FMOD es el
            //    padre de todos los canales, los abra quien los abra. Un stop() ahi
            //    no depende de que GD tenga la pista registrada como musica, que es
            //    justo donde fallaban los niveles con varias canciones.
            if (fmod->m_system) {
                FMOD::ChannelGroup* master = nullptr;
                if (fmod->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) {
                    master->stop();
                }
            }
        }

        // El tiron de construir el nivel ocurre en esta linea. Por eso va aqui y no
        // antes: detras del negro no se ve.
        auto scene = PlayLayer::scene(m_level, false, false);

        // Velo de entrada: nace opaco dentro de la escena nueva y se abre.
        auto veil = CCLayerColor::create({0, 0, 0, 255});
        veil->setContentSize(CCDirector::get()->getWinSize());
        scene->addChild(veil, 9999);

        veil->runAction(CCSequence::create(
            CCFadeTo::create(kFadeIn, 0),
            CCCallFuncN::create(this, callfuncN_selector(SheepLevelLauncher::onVeilDone)),
            nullptr
        ));

        // Aviso para el PlayLayer que llega: este nivel lo puso un salto (lo usa para
        // corregir el cursor).
        sheep::swappedByAutoSkip() = true;

        // Sin transicion: el velo es la transicion. Una CCTransitionScene encima solo
        // anadiria un fundido compitiendo con el del mod.
        CCDirector::get()->replaceScene(scene);

        m_level->release();
        m_level = nullptr;
    }

    // Final del velo de entrada. CCCallFuncN recibe el nodo que ejecuta la accion,
    // es decir, el propio velo.
    void onVeilDone(CCNode* veil) {
        if (veil) veil->removeFromParent();
        this->autorelease();   // trabajo terminado: se libera la referencia propia
    }

    // Fallo de una descarga. Aqui llega el codigo de error, no el id del nivel, asi
    // que no se puede comprobar de quien era el fallo, al contrario que en
    // levelDownloadFinished. Lo unico que se sabe es si este lanzador tenia una
    // descarga en marcha. Si no, el fallo es de otro (el caso tipico: una descarga
    // del precargador que estaba en curso cuando se le quito el hueco), y cancelar
    // el salto por eso dejaria al streamer sin cambio de nivel por un error ajeno.
    void levelDownloadFailed(int) override {
        if (!m_downloading) return;
        m_downloading = false;
        fail("Fail_DownloadFailed");
    }

private:

    // El CustomSongId del JSON para ese nivel, buscado en el actual y en la cola.
    // Solo se usa si el nivel no trae m_songIDs (niveles antiguos, o respuestas
    // del servidor que no los incluyen).
    static int fallbackFor(int levelId) {
        auto const& d = SheepData::get()->data();
        if (d.current.levelIdAsInt() == levelId) return d.current.customSongIdAsInt();
        for (auto const& lv : d.queue)
            if (lv.levelIdAsInt() == levelId) return lv.customSongIdAsInt();
        return 0;
    }

    // El PlayLayer de la escena en pantalla, o nullptr. Se busca entre los hijos de
    // la escena porque PlayLayer::get() esta "Out of line" en los bindings y no se
    // puede llamar.
    static PlayLayer* findPlayLayer() {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return nullptr;
        auto kids = scene->getChildren();
        if (!kids) return nullptr;
        for (unsigned int i = 0; i < kids->count(); ++i) {
            if (auto pl = geode::cast::typeinfo_cast<PlayLayer*>(kids->objectAtIndex(i)))
                return pl;
        }
        return nullptr;
    }

    // Suelta los huecos del delegado, solo si los ocupa este lanzador.
    void detach() {
        auto glm = GameLevelManager::sharedState();
        if (!glm) return;
        if (glm->m_levelManagerDelegate  == this) glm->m_levelManagerDelegate  = nullptr;
        if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    }

    // Cancela el salto. Si algo falla no se toca la escena: el jugador sigue en su
    // nivel y el motivo queda en el log. Mostrar un aviso en mitad de la partida
    // seria peor.
    //
    // 'why' es una llave de traduccion (Fail_...), no una frase. Se traduce aqui, y
    // no en cada llamada, para no repetir el mismo sheep::tr() en cada motivo.
    //
    // Desprograma todas las esperas: el scheduler retiene al lanzador, y una que
    // quedara programada lo mantendria vivo.
    void fail(char const* why) {
        stopAudioWait();
        stopNetTimeout();
        stopBlackWait();
        m_downloading = false;
        log::warn("[SheepGD] {}", sheep::trf("Log_SkipCancelled", m_levelId, sheep::tr(why)));
        detach();
        if (m_level) { m_level->release(); m_level = nullptr; }
        this->autorelease();
    }

    int m_levelId = 0;
    bool m_downloading = false;   // hay una descarga propia en curso (ver levelDownloadFailed)
    static constexpr float kFadeOut = 0.35f;   // partida vieja -> negro
    static constexpr float kFadeIn  = 0.35f;   // negro -> partida nueva
    static constexpr float kNetTimeout = 10.f; // busqueda + descarga, en segundos

    int   m_fallbackSongId = 0;     // cancion del JSON por si el nivel no trae la suya
    float m_waited         = 0.f;   // segundos de espera de musica acumulados

    GJGameLevel* m_level = nullptr;   // nivel descargado, retenido hasta swapNow

    // La partida desde la que se pidio el salto. Solo se compara, nunca se lee: si
    // el jugador salio, ya no existe.
    PlayLayer* m_fromLayer = nullptr;
};