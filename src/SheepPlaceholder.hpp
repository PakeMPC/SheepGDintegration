#pragma once
#include <Geode/Geode.hpp>
#include "SheepAssets.hpp"
#include "QueueData.hpp"
#include "SheepI18n.hpp"

using namespace geode::prelude;

// La ficha del nivel placeholder, para mostrarlo en la tarjeta de la escena
// cuando la cola se acaba y el modo de fin de cola es "Placeholder".
//
// El placeholder no es una peticion: el JSON no trae nada suyo, solo se guarda su
// ID en los ajustes. Asi que el nombre, el autor, la dificultad y lo demas hay que
// pedirselos al juego y guardarlos aqui.
//
// La ficha se busca en tres sitios, del mas barato al mas caro (ver step): la
// copia local del nivel, la cache de busquedas de GD y, solo si hace falta, una
// busqueda en los servidores.
//
// Es un singleton retenido, como el precargador y por el mismo motivo: espera una
// respuesta de red que puede tardar mas de lo que dura la escena que la pidio.
class SheepPlaceholder : public CCObject, public LevelManagerDelegate {
public:
    // La instancia unica. Se crea la primera vez que se pide.
    static SheepPlaceholder* get() {
        static SheepPlaceholder* inst = [] {
            auto p = new SheepPlaceholder();
            p->retain();          // dura lo que dura el juego
            return p;
        }();
        return inst;
    }

    // Devuelve la ficha si ya esta lista, o nullptr mientras se consigue. Ademas
    // avanza la busqueda un paso, asi que hay que llamarla periodicamente (lo hace
    // la escena en su tick), igual que las descargas de canciones.
    //
    // Si cambia el ID, se descarta la ficha anterior y se empieza de cero. Si se
    // agotan los intentos, devuelve nullptr hasta que cambie el ID o se reinicie
    // el juego, y la tarjeta se queda con el WAITING de siempre.
    LevelModel const* fetch(int id) {
        if (id <= 0) return nullptr;
        if (id != m_id) { reset(); m_id = id; }
        if (m_ready) return &m_model;
        step();
        return nullptr;
    }

    // Una accion de primer plano necesita el hueco del delegado: el buscador se
    // aparta. Lo llama SheepQueueLayer::requestLevel antes de ocupar el hueco.
    //
    // GameLevelManager tiene un solo hueco para el delegado de busquedas. Quitarselo
    // al buscador sin avisar tambien funciona, pero lo deja esperando para siempre
    // una respuesta que ya no le va a llegar (no tiene watchdog). Y el intento se
    // devuelve, porque el fallo no fue suyo.
    void yield() {
        if (!m_searching) return;
        detach();
        m_searching = false;
        if (m_tries > 0) --m_tries;
    }

    // --- LevelManagerDelegate ---

    // Resultado de la busqueda: si el nivel esta entre los resultados, se adopta.
    void loadLevelsFinished(CCArray* levels, char const*) override {
        detach();
        m_searching = false;
        if (!levels) return;

        for (unsigned int i = 0; i < levels->count(); ++i) {
            auto lv = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if (lv && lv->m_levelID == m_id) { adopt(lv); return; }
        }
        // No estaba: se reintenta en la siguiente llamada a fetch, hasta agotar los
        // intentos.
    }
    void loadLevelsFinished(CCArray* l, char const* k, int) override { loadLevelsFinished(l, k); }
    // Fallo de la busqueda: queda registrado, y se reintenta en la siguiente
    // llamada a fetch mientras queden intentos.
    void loadLevelsFailed(char const*) override {
        detach();
        m_searching = false;
        log::warn("[SheepGD] {}", sheep::trf("Log_PlaceholderFailed", m_id));
    }
    void loadLevelsFailed(char const* k, int) override { loadLevelsFailed(k); }

private:
    static constexpr int kMaxTries = 3;   // busquedas en la red como maximo, por ID

    SheepPlaceholder() = default;

    // Olvida la ficha y la busqueda en curso (al cambiar de ID).
    void reset() {
        detach();
        m_ready = false; m_searching = false; m_tries = 0;
        m_model = LevelModel{};
    }

    // Suelta el hueco del delegado, solo si lo ocupa este buscador.
    void detach() {
        auto glm = GameLevelManager::sharedState();
        if (glm && glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
    }

    // Un paso de la busqueda. No hace nada si ya hay una busqueda en la red en
    // curso o si se agotaron los intentos.
    void step() {
        if (m_searching || m_tries >= kMaxTries) return;

        auto glm = GameLevelManager::sharedState();
        if (!glm) return;

        // 1. La copia local. Un placeholder es un nivel que el streamer juega, asi
        //    que lo normal es que GD ya lo tenga y no haya que usar la red. Se
        //    comprueba el nombre porque una ficha incompleta no sirve.
        if (auto saved = glm->getSavedLevel(m_id)) {
            if (!saved->m_levelName.empty()) { adopt(saved); return; }
        }

        // 2. La cache de busquedas de GD, la misma que usa la escena.
        auto search = GJSearchObject::create(SearchType::Search, std::to_string(m_id));
        if (auto stored = glm->getStoredOnlineLevels(search->getKey())) {
            loadLevelsFinished(stored, "");
            return;
        }

        // 3. Y solo entonces, la red. Como es una tarea de segundo plano, solo ocupa
        //    el hueco del delegado si esta libre. Si no, ni se intenta (no se gasta
        //    intento) y se reintenta en la siguiente llamada.
        if (glm->m_levelManagerDelegate) return;

        ++m_tries;
        m_searching = true;
        glm->m_levelManagerDelegate = this;
        glm->getOnlineLevels(search);
    }

    // Guarda la ficha a partir del nivel encontrado y la marca como lista.
    void adopt(GJGameLevel* lv) {
        m_model = fromGameLevel(lv, m_id);
        m_ready = true;
        log::debug("[SheepGD] {}", sheep::trf("Log_PlaceholderReady", m_id, m_model.name));
    }

    // Numero compacto para descargas y likes, con el mismo criterio que
    // SheepGDRequest: trunca en vez de redondear, para que 999.999 no salga como
    // "1000K", y usa siempre punto decimal. 1234 -> "1.2K", 2500000 -> "2.5M".
    static std::string compact(int n) {
        if (n < 0) n = 0;
        if (n >= 1000000) { int e = n / 100000; return fmt::format("{}.{}M", e / 10, e % 10); }
        if (n >= 1000)    { int e = n / 100;    return fmt::format("{}.{}K", e / 10, e % 10); }
        return std::to_string(n);
    }

    // Nombre del subtipo de demon a partir de GJGameLevel::m_demonDifficulty, en el
    // formato que entiende sheep::difficultyIndex.
    static char const* demonName(int d) {
        switch (d) {
            case 3:  return "EasyDemon";
            case 4:  return "MediumDemon";
            case 5:  return "InsaneDemon";
            case 6:  return "ExtremeDemon";
            default: return "HardDemon";   // el 0 es el Hard Demon original
        }
    }

    // Nombre de la duracion a partir de GJGameLevel::m_levelLength. "Platformer" es
    // el que reconoce sheep::isPlatformer.
    static char const* lengthName(int len) {
        switch (len) {
            case 0: return "Tiny";  case 1: return "Short"; case 2: return "Medium";
            case 3: return "Long";  case 4: return "XL";    case 5: return "Platformer";
            default: return "";
        }
    }

    // Traduce un GJGameLevel al LevelModel que entiende la tarjeta.
    //
    // Nota: este modelo no lleva 'raw'. Si alguna vez acabara en data().current y
    // se guardara, saveToFile escribiria un CurrentLevel nulo y se perderia la
    // peticion real. No es una peticion y nunca debe tocar el JSON: solo viaja a la
    // tarjeta.
    static LevelModel fromGameLevel(GJGameLevel* lv, int id) {
        LevelModel m;
        m.levelId = std::to_string(id);
        m.name    = lv->m_levelName;
        m.author  = lv->m_creatorName;
        m.stars   = lv->m_stars.value();   // SeedValueRSV: valor ofuscado, se lee con .value()

        // De la tabla de estrellas, no de getBaseCurrencyForLevel: esa devuelve 400
        // donde van 500, y hacia que este mismo nivel saliera "40/400" como
        // placeholder y "40/500" como peticion.
        m.orbs = sheep::orbsForStars(m.stars);

        // Con demon se da el subtipo exacto; sin el se deja vacio y difficultyIndex
        // lo deduce de las estrellas, que es su red de seguridad.
        //
        // Un nivel sin valorar (0 estrellas) sale con la cara N/A, aunque GD le
        // muestre la dificultad votada. Es a proposito: SheepGDRequest lo trata
        // igual (DifficultyName "UNRATED"), y asi la cara es la misma entre por
        // donde entre el nivel.
        if (lv->m_demon.value() > 0) m.difficultyName = demonName(lv->m_demonDifficulty);

        m.length      = lengthName(lv->m_levelLength);
        m.customSongId = std::to_string(lv->m_songID);
        // El signo va en isDislike y el numero siempre positivo, como en el JSON:
        // compact pone a 0 los negativos, asi que se le pasa el valor absoluto.
        m.downloads   = compact(lv->m_downloads);
        m.likes       = compact(std::abs(lv->m_likes));
        m.isDislike   = lv->m_likes < 0;

        m.coins    = lv->m_coins;
        m.hasCoin1 = lv->m_coins >= 1;
        m.hasCoin2 = lv->m_coins >= 2;
        m.hasCoin3 = lv->m_coins >= 3;
        
        // sheep::featureState solo mira si el texto contiene la palabra, asi que no
        // hace falta construir una ruta avares: basta con la palabra.
        m.hasFeatureGlow = lv->m_featured > 0 || lv->m_isEpic > 0;
        m.featureGlowPath = lv->m_isEpic >= 3 ? "mythic"
                          : lv->m_isEpic == 2 ? "legendary"
                          : lv->m_isEpic == 1 ? "epic"
                                              : "featured";

        // La misma URL que construye sheep::thumbUrl cuando el JSON no trae ninguna.
        m.thumbnailUrl = fmt::format("https://levelthumbs.prevter.me/thumbnail/{}", id);

        // Se quedan vacios a proposito: nadie lo pidio, no hay plataforma de origen
        // ni temporizador, y las listas comunitarias vacias significan "no dibujar
        // la insignia".
        return m;
    }

    int  m_id        = 0;       // ID del placeholder que se esta buscando
    bool m_ready     = false;   // m_model ya esta completo
    bool m_searching = false;   // hay una busqueda en la red en curso
    int  m_tries     = 0;       // busquedas en la red gastadas para este ID
    LevelModel m_model;
};