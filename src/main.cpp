// ---------------------------------------------------------------------------
// Punto de entrada del mod: los hooks sobre clases de Geometry Dash.
//
// - MyMenuLayer: el boton de la oveja en el menu principal, que abre la cola.
// - SheepPlayLayer: la partida lanzada desde la cola. Vigila el JSON para el
//   auto-skip, dibuja el reloj, decide que pasa cuando se acaba la cola y
//   gestiona la vuelta a la escena del mod.
// - SheepPlayerObject: sustituye el dibujo del cubo por Sheepy en la partida.
// - SheepGarageLayer y SheepColorPage: Sheepy en el taller de iconos y en el
//   selector de colores.
// - SheepKeyboard: las teclas Q y E, para cambiar de nivel sin salir de la
//   partida.
//
// La logica que no necesita engancharse a una clase del juego vive en las
// cabeceras Sheep*.hpp; aqui queda solo lo que depende de un hook.
// ---------------------------------------------------------------------------
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "SheepQueuePopup.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/CharacterColorPage.hpp>
#include "SheepIcon.hpp"
#include "SheepAssets.hpp"
#include "SheepData.hpp"
#include "SheepLauncher.hpp"
#include "SheepTime.hpp"
#include "SheepI18n.hpp"

using namespace geode::prelude;

// Constantes y utilidades internas de este archivo. El espacio de nombres anonimo
// les da enlace interno: no son visibles desde otros archivos del mod.
namespace {
    constexpr float kSheepTick        = 0.25f;  // sondeo del auto-skip y del reloj
    constexpr float kIngameTimerScale = 0.55f;  // escala del numero del reloj
    constexpr float kIngameTimerTop   = 44.f;   // distancia desde el borde superior
    constexpr float kIngameTimerIcon  = 18.f;   // alto del reloj
    constexpr float kIngameTimerGap   = 6.f;    // reloj -> numero
    constexpr int   kLastSeconds      = 10;     // ventana del modo "Last 10s"

    // Si se pidio un salto y la partida sigue en pie pasado este tiempo, el
    // lanzamiento fallo (ver el watchdog de sheepTick).
    constexpr float kSwapGiveUp = 10.f;

    // Fundido de salida de las dos salidas automaticas (fin de cola y muerte).
    // La salida por el menu de pausa no lo lleva: ver el comentario de onQuit.
    constexpr float kQuitFade   = 0.35f;

    // Margen antes de irse, para que de tiempo a ver lo que acaba de pasar.
    constexpr float kEndPause   = 0.25f;   // el contador llegando a 0:00
    constexpr float kDeathPause = 0.50f;   // la animacion de explosion entera

    // Indica si la partida esta en pausa. La mantienen los hooks de pauseGame y
    // resume de SheepPlayLayer. Hace falta porque PlayLayer::get() esta marcada
    // como "Out of line" en los bindings y no se puede llamar, asi que el hook del
    // teclado no tiene otra forma de preguntarlo.
    bool g_sheepPaused = false;

    // Mueve la cola un paso desde el teclado: hacia delante (E) o hacia atras (Q).
    // Devuelve true si la cola cambio y se guardo en el JSON.
    //
    // No lanza el nivel: solo escribe el JSON. El vigia de SheepPlayLayer
    // (sheepTick) ve el requestId nuevo en el siguiente tick y dispara el salto
    // por su cuenta. Llamar aqui a play() directamente haria que el vigia lo viera
    // tambien, y el nivel se lanzaria dos veces.
    bool sheepQueueStep(bool adelante) {
        auto svc = SheepData::get();
        if (!svc->hasPath()) return false;

        auto& d = svc->data();

        // Enfriamiento de un segundo entre cambios de nivel (sheep::trySkip, en
        // SheepTime.hpp), compartido con las flechas de la escena. Se comprueba
        // despues de saber si hay a donde saltar y antes de mutar la cola: asi una
        // E con la cola vacia no gasta el turno del salto valido que venga detras.
        bool hayDestino = adelante ? !d.queue.empty() : !d.history.empty();
        if (hayDestino && !sheep::trySkip()) {
            log::debug("[SheepGD] {}", sheep::tr("Log_SkipTooFast"));
            return false;
        }

        if (adelante) {
            if (d.queue.empty()) {
                log::debug("[SheepGD] {}", sheep::tr("Log_KeyQueueEmpty"));
                return false;
            }
            d.skipToNext();
        } else {
            if (d.history.empty()) {
                log::debug("[SheepGD] {}", sheep::tr("Log_KeyNoPrevious"));
                return false;
            }
            d.backToPrevious();
        }

        // Si la escritura falla, save() deshace el paso: el vigia de la partida no
        // llega a ver un nivel que el JSON no tiene.
        if (!svc->save()) {
            log::warn("[SheepGD] {}", sheep::tr("Log_KeyWriteFailed"));
            return false;
        }

        log::info("[SheepGD] {}", sheep::tr(adelante ? "Log_KeyNext" : "Log_KeyPrev"));
        return true;
    }
}

// Menu principal: anade el boton de la oveja, que abre la escena de la cola.
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto winSize = CCDirector::get()->getWinSize();

        // 1. Un CCMenu propio para el boton, aparte de los menus del juego.
        auto sheepMenu = CCMenu::create();
        sheepMenu->setID("sheep-menu"_spr);

        // 2. Arriba a la derecha, a 40 puntos de cada borde.
        // Posicion fija a proposito, y no colgado de un menu del juego: otro mod que
        // use esa esquina se solaparia con el boton, pero se decidio dejarlo asi.
        sheepMenu->setPosition({winSize.width - 40.f, winSize.height - 40.f});

        auto sheepSprite = CCSprite::create("sheep_btn.png"_spr);
        auto sheepBtn = CCMenuItemSpriteExtra::create(
            sheepSprite,
            this,
            menu_selector(MyMenuLayer::onOpenSheepQueue)
        );

        sheepBtn->setID("sheep-queue-button"_spr);
        sheepMenu->addChild(sheepBtn);

        // 3. El menu se anade directamente al MenuLayer.
        this->addChild(sheepMenu);

        return true;
    }

    // Abre la escena de la cola con un fundido. Va con pushScene: el boton de
    // volver de la escena la desapila y se regresa a este menu.
    void onOpenSheepQueue(CCObject* sender) {
        // Con un JSON ya enlazado se entra directamente a la escena.
        if (QueueData::hasLinkedFile()) {
            auto scene = CCTransitionFade::create(0.5f, SheepQueueLayer::scene());
            CCDirector::get()->pushScene(scene);
            return;

        }

        // Si no, se pide el archivo antes de entrar: asi no se ve la escena vacia
        // de fondo mientras se usa el selector de archivos del sistema.
        QueueData::promptForFile([](std::filesystem::path const&) {
            auto scene = CCTransitionFade::create(0.5f, SheepQueueLayer::scene());
            CCDirector::get()->pushScene(scene);
        });
    }

};

// La partida. Todo lo de esta clase se activa solo si el nivel se lanzo desde la
// cola del mod, que es lo que indica SheepQueueLayer::s_returnTicket (el "billete
// de vuelta"): se marca justo antes de lanzar el nivel y se consume al salir.
//
// Responsabilidades:
// - Vigilar el JSON cada kSheepTick segundos y saltar al nivel nuevo cuando
//   cambia el actual (auto-skip, teclas Q/E o cambios hechos en SheepGDRequest).
// - Dibujar el reloj del auto-skip sobre la partida.
// - Decidir que pasa cuando se acaba la cola (handleEndOfQueue).
// - Volver a la escena del mod al salir, con el audio en orden (onQuit).
//
// Nota: cada salto de nivel crea un PlayLayer nuevo, asi que lo que tenga que
// sobrevivir de un nivel a otro no puede vivir en los Fields: vive en SheepData.hpp
// (sheep::inPlaceholder(), sheep::swappedByAutoSkip()).
class $modify(SheepPlayLayer, PlayLayer) {
    struct Fields {
        std::string    m_watchedRequestId;       // peticion que se esta jugando
        bool           m_swapping    = false;    // hay un salto o una salida en curso
        CCNode*        m_skipBox     = nullptr;  // contenedor del reloj (icono + numero)
        CCSprite*      m_skipIcon    = nullptr;  // icono del reloj
        CCLabelBMFont* m_skipTimer   = nullptr;  // numero del reloj
        bool           m_lastTenOnly = false;    // ajuste "Last 10s", leido al entrar

        // Fin de cola y momento del cambio de nivel ("swap on death").
        int   m_pendingLevelId = 0;      // nivel esperando a que el jugador muera
        bool  m_exitOnDeath    = false;  // salir a la lista al morir
        bool  m_deathPending   = false;  // murio: decidir en el siguiente tick
        float m_swapWait       = 0.f;    // watchdog del salto en vuelo
        // El fin de cola se decide una sola vez. No puede detectarse por cambio de
        // requestId como un nivel nuevo, porque al vaciarse la cola el requestId no
        // cambia (ver checkQueueSwap), asi que hace falta un pestillo.
        bool  m_endHandled     = false;
        bool  m_quitting       = false;  // ya hay una salida con fundido en marcha
        bool  m_autoQuit       = false;  // la salida la decidimos nosotros, no el menu de pausa
    };

    // Reinicia el nivel para que GD vuelva a bloquear el cursor (ver startGame),
    // conservando el contador de intentos.
    void relockCursor() {
        // El reinicio es un recurso para recuperar el cursor, no un intento del
        // jugador, asi que el contador se devuelve a su valor. m_attempts lo hereda
        // PlayLayer de GJBaseGameLayer y es publico; la etiqueta hay que repintarla
        // a mano porque GD ya la escribio con un intento de mas.
        int intentos = m_attempts;

        PlayLayer::resetLevel();

        m_attempts = intentos;
        if (m_attemptLabel && m_attemptLabel->getString()) {
            // Solo se cambia el numero y se respeta el resto del texto: si otro mod
            // traduce el juego, la palabra no tiene por que ser "Attempt". El numero
            // es la ultima tira de cifras. Sin cifras, la etiqueta no se toca.
            std::string texto = m_attemptLabel->getString();
            auto fin = texto.find_last_of("0123456789");
            if (fin != std::string::npos) {
                auto ini = texto.find_last_not_of("0123456789", fin);
                ini = (ini == std::string::npos) ? 0 : ini + 1;
                texto.replace(ini, fin - ini + 1, fmt::format("{}", m_attempts));
                m_attemptLabel->setString(texto.c_str());
            }
        }
    }

    // Arranque del nivel. Ademas de lo normal, corrige el cursor tras un salto.
    //
    // Tras un salto de nivel el cursor queda visible aunque el "lock cursor" del
    // juego este activo. El mod no puede tocar el cursor directamente: las tres
    // funciones de PlatformToolbox que lo harian (hideCursor, showCursor,
    // toggleLockCursor) estan "Out of line" en Windows. Asi que se consigue que lo
    // haga GD. Las dos cosas que lo curan, pausar y despausar o morir y que el
    // nivel se reinicie, tienen en comun un resetLevel sobre una capa que ya esta
    // corriendo. En el salto tambien hay un resetLevel, pero sale desde dentro de
    // init, con la capa a medio montar, y ahi no surte efecto.
    //
    // Es una solucion empirica: no se sabe por que el resetLevel de init no basta
    // y el de una capa en marcha si. Si deja de funcionar, el siguiente paso es
    // mirar que hace ClipCursor de Wine durante el cambio de escena.
    void startGame() {
        PlayLayer::startGame();

        // Va despues de startGame y un frame mas tarde, para no meter un reinicio
        // en mitad de la secuencia de arranque del nivel.
        if (!sheep::swappedByAutoSkip()) return;
        sheep::swappedByAutoSkip() = false;

        this->runAction(CCSequence::create(
            CCDelayTime::create(0.f),
            CCCallFunc::create(this, callfunc_selector(SheepPlayLayer::relockCursor)),
            nullptr
        ));
    }

    // Al entrar al nivel: si la partida viene de la cola, arma el vigia
    // (sheepTick) y, segun los ajustes, el reloj del auto-skip.
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        g_sheepPaused = false;   // nivel nuevo, empieza corriendo

        // Solo se actua si la partida viene de la cola: el billete se marca justo
        // antes de lanzar el nivel y no se consume hasta salir.
        if (!SheepQueueLayer::s_returnTicket) return true;

        auto const& cur = SheepData::get()->data().current;

        // Sin nivel actual valido no hay nada que vigilar. El placeholder es la
        // excepcion: ahi CurrentLevel es el centinela WAITING y la partida existe
        // precisamente para esperar a que entre una peticion. Sin esta salvedad el
        // vigia no se armaria y el placeholder se quedaria puesto para siempre.
        if (!cur.isValid() && !sheep::inPlaceholder()) return true;

        // requestId y no levelId: el mismo nivel puede estar pedido dos veces y
        // son peticiones distintas.
        m_fields->m_watchedRequestId = cur.requestId;

        // Si el tiempo del nivel ya estaba agotado antes de entrar, ese fin de cola
        // no es de esta partida. El caso tipico es abrir GD con SheepGDRequest
        // cerrado, con el JSON congelado y el plazo vencido desde hace horas. Sin
        // esto, "EXIT TO LIST" sacaria al jugador del nivel en el primer tick.
        m_fields->m_endHandled = endOfQueueNow();

        // El ajuste se lee una vez, al entrar: cambiarlo a media partida no debe
        // hacer aparecer o desaparecer elementos de golpe.
        m_fields->m_lastTenOnly = sheep::lastTenOnly();
        if (sheep::ingameTimerEnabled()) buildSkipTimer();

        this->schedule(schedule_selector(SheepPlayLayer::sheepTick), kSheepTick);
        return true;
    }

    // Construye el reloj del auto-skip, arriba en el centro. Nace oculto; lo
    // muestra y actualiza updateSkipTimer.
    void buildSkipTimer() {
        // Un contenedor con el reloj y el numero dentro. Hace falta porque el texto
        // cambia de ancho cada segundo, y para que el conjunto siga centrado hay que
        // recolocar las dos piezas en cada actualizacion.
        auto box = CCNode::create();
        auto win = CCDirector::get()->getWinSize();
        box->setPosition({win.width / 2.f, win.height - kIngameTimerTop});
        box->setVisible(false);
        this->addChild(box, 1000);

        if (auto icon = sheep::iconLength()) {   // GJ_timeIcon_001: el reloj
            icon->setScale(kIngameTimerIcon / std::max(1.f, icon->getContentSize().height));
            icon->setAnchorPoint({0.f, 0.5f});
            box->addChild(icon);
            m_fields->m_skipIcon = icon;
        }

        auto lbl = CCLabelBMFont::create("", "bigFont.fnt");
        lbl->setScale(kIngameTimerScale);
        lbl->setAnchorPoint({0.f, 0.5f});
        box->addChild(lbl);

        m_fields->m_skipBox   = box;
        m_fields->m_skipTimer = lbl;
    }

    // El vigia, cada kSheepTick segundos. Es un schedule del propio PlayLayer, asi
    // que se detiene con la pausa.
    void sheepTick(float dt) {
        updateSkipTimer();

        // Watchdog. Si hay un salto pedido y la partida sigue aqui pasados
        // kSwapGiveUp segundos, el lanzamiento fallo (nivel inexistente, red caida,
        // un ID de placeholder mal escrito) y hay que volver a vigilar. Antes el
        // vigia se desprogramaba al pedir el salto, y ese fallo dejaba la partida
        // sin reaccionar a la cola para siempre.
        if (m_fields->m_swapping) {
            m_fields->m_swapWait += dt;
            if (m_fields->m_swapWait >= kSwapGiveUp) {
                log::warn("[SheepGD] {}", sheep::tr("Log_SwapNeverHappened"));
                m_fields->m_swapping = false;
                m_fields->m_swapWait = 0.f;
                sheep::inPlaceholder() = false;
            }
            return;
        }

        // La muerte se atiende aqui y no dentro de destroyPlayer: ahi GD sigue
        // procesando el golpe, y cambiar de escena desde dentro de su propia
        // llamada destruiria objetos que el juego todavia esta usando.
        if (m_fields->m_deathPending) {
            m_fields->m_deathPending = false;
            onDeathAction();
            if (m_fields->m_swapping) return;
        }

        checkQueueSwap();
    }

    // Muestra, oculta y recentra el reloj del auto-skip.
    void updateSkipTimer() {
        auto box = m_fields->m_skipBox;
        auto lbl = m_fields->m_skipTimer;
        if (!box || !lbl) return;

        // El reloj es del nivel que se esta jugando, y todo lo de abajo da por
        // hecho que ese nivel es el actual del JSON. Hay dos momentos en que deja
        // de serlo, y en los dos se oculta:
        //
        //  - m_pendingLevelId: "swap on death". El auto-skip vencio y SheepGDRequest
        //    ya avanzo la cola, pero la partida sigue en el nivel viejo esperando a
        //    que el jugador muera. El plazo que trae el JSON es el del siguiente
        //    nivel, asi que pintarlo seria un reloj contando para un nivel que el
        //    jugador todavia no ha visto.
        //  - m_swapping: el salto ya se pidio y la pantalla se esta fundiendo. Es
        //    el mismo caso, durante medio segundo.
        //
        // El reloj vuelve solo: m_pendingLevelId es un campo de esta capa, y la que
        // monta el nivel nuevo nace con el a cero.
        if (m_fields->m_pendingLevelId > 0 || m_fields->m_swapping) {
            box->setVisible(false);
            return;
        }

        auto const& cur = SheepData::get()->data().current;
        auto left = sheep::secondsLeft(cur.autoSkipEndsAt);

        if (left <= 0 || (m_fields->m_lastTenOnly && left > kLastSeconds)) {
            box->setVisible(false);
            return;
        }

        box->setVisible(true);
        lbl->setString(sheep::formatClock(left).c_str());
        lbl->setColor(left <= kLastSeconds ? ccColor3B{255, 90, 90}
                                           : ccColor3B{255, 255, 255});

        // Recentrado. El ancho del conjunto lo decide el texto, que cambia por
        // ejemplo al pasar de 1:00 a 59, asi que se recalcula en cada tick y las
        // dos piezas se colocan desde el borde izquierdo del total.
        auto icon = m_fields->m_skipIcon;
        float iw    = icon ? icon->getScaledContentSize().width : 0.f;
        float gap   = icon ? kIngameTimerGap : 0.f;
        float total = iw + gap + lbl->getScaledContentSize().width;

        if (icon) icon->setPositionX(-total / 2.f);
        lbl->setPositionX(-total / 2.f + iw + gap);
    }

    // Fin de cola: no queda nada en la cola y el nivel actual es el centinela
    // WAITING o ya agoto su tiempo. La condicion vive en SheepData.hpp
    // (sheep::queueEnded) porque la escena hace la misma pregunta para decidir si
    // muestra el placeholder en la tarjeta.
    bool endOfQueueNow() { return sheep::queueEnded(SheepData::get()->data()); }

    // Compara el JSON con lo que se esta jugando y decide si hay que saltar de
    // nivel o atender el fin de cola.
        void checkQueueSwap() {
        auto const& cur = SheepData::get()->data().current;
        int id = cur.levelIdAsInt();

        // --- 1. Entro un nivel jugable nuevo ----------------------------
        // Aqui el requestId si es una senal fiable: cada peticion trae su propio
        // GUID, y el mismo nivel pedido dos veces son dos peticiones.
        //
        // Nota sobre la forma de esta condicion. La version anterior salia con un
        // return cuando el requestId coincidia, y eso hacia inalcanzable el fin de
        // cola: al expirar el auto-skip sin cola, CurrentLevel sigue siendo este
        // mismo nivel valido, asi que cada tick entraba aqui, veia el requestId de
        // siempre y salia. "Es el mismo nivel" y "a ese nivel se le acabo el
        // tiempo" son estados simultaneos, no excluyentes.
        bool nuevo = cur.isValid() && id > 0
                  && cur.requestId != m_fields->m_watchedRequestId;

        if (nuevo) {
            m_fields->m_watchedRequestId = cur.requestId;

            // Cualquier plan de fin de cola se cancela: ya hay cola otra vez.
            m_fields->m_endHandled  = false;
            m_fields->m_exitOnDeath = false;
            sheep::inPlaceholder()  = false;

            if (sheep::swapOnDeath()) {
                // "Swap on death": el JSON ya avanzo (eso lo decide SheepGDRequest)
                // y aqui solo se retrasa el lanzamiento. Que el overlay de OBS vaya
                // por delante de lo que se ve en pantalla es una divergencia
                // aceptada a proposito.
                log::info("[SheepGD] {}", sheep::trf("Log_SwapOnDeath", id));
                m_fields->m_pendingLevelId = id;
                return;
            }
            launchNow(id);
            return;
        }

        // --- 2. Fin de cola ---------------------------------------------
        // Se llega aqui tanto con el centinela WAITING como con el nivel de siempre
        // ya caducado, que es el caso importante. El fin de cola es un estado y no
        // una transicion: al vaciarse la cola el requestId no cambia. Por
        // auto-skip, SheepGDRequest no escribe nada si no queda cola (el nivel
        // actual se queda con su plazo vencido), y con la tecla E el centinela
        // WAITING se fabrica copiando los datos del nivel anterior, con su
        // RequestId incluido.
        if (m_fields->m_endHandled) return;
        if (!endOfQueueNow())       return;

        // El pestillo se echa antes de actuar: handleEndOfQueue puede llamar a
        // onQuit, y volver a entrar aqui con la capa ya de salida repetiria la
        // decision.
        m_fields->m_endHandled     = true;
        m_fields->m_pendingLevelId = 0;   // no hay a donde saltar
        log::info("[SheepGD] {}", sheep::trf("Log_EndDetected",
            sheep::endOfQueueName(sheep::endOfQueueMode())));
        handleEndOfQueue();
    }

    // Aplica el modo de fin de cola elegido en los ajustes. Se decide una sola vez,
    // al quedarse la cola vacia:
    // - kEndStay: seguir en el nivel y vigilar hasta que entre una peticion.
    // - kEndExitToList: salir a la lista tras kEndPause.
    // - kEndUntilDeath: seguir jugando y salir a la lista al morir.
    // - kEndPlaceholder: ir a un nivel fijo hasta que entre una peticion.
    void handleEndOfQueue() {
        // Si ya estamos en el placeholder no hay nada que replantear: la partida
        // existe justo para esperar, y volver a lanzarlo seria un bucle.
        if (sheep::inPlaceholder()) return;

        switch (sheep::endOfQueueMode()) {
            case sheep::kEndExitToList:
                log::info("[SheepGD] {}", sheep::tr("Log_EndExit"));
                quitAfter(kEndPause);
                break;

            case sheep::kEndUntilDeath:
                log::info("[SheepGD] {}", sheep::tr("Log_EndOnDeath"));
                m_fields->m_exitOnDeath = true;
                break;

            case sheep::kEndPlaceholder: {
                int pid = sheep::placeholderLevelId();
                if (pid <= 0) {
                    // Sin ID valido se queda en el nivel, como con kEndStay. Es
                    // preferible a sacar al streamer de la partida.
                    log::warn("[SheepGD] {}", sheep::tr("Log_EndNoPlaceholder"));
                    break;
                }
                log::info("[SheepGD] {}", sheep::trf("Log_EndPlaceholder", pid));
                sheep::inPlaceholder() = true;
                launchNow(pid);
                break;
            }

            default:   // kEndStay: no se toca la partida y se sigue vigilando
                break;
        }
    }

    // Salida automatica: espera 'delay' segundos y despues funde imagen y sonido.
    // Los dos tiempos son distintos a proposito: primero se mantiene la imagen,
    // para que se vea el 0:00 o la explosion entera, y solo entonces se oscurece.
    // Fundir durante la espera taparia justo lo que se queria mostrar.
    //
    // Aqui si se puede esperar, al contrario que en la salida por el menu de pausa:
    // la decision es del mod y GD todavia no ha llamado a nada suyo.
    void quitAfter(float delay) {
        m_fields->m_swapping = true;   // hay algo en curso: el vigia deja de actuar
        m_fields->m_swapWait = 0.f;
        m_fields->m_autoQuit = true;   // lo mira onQuit para el audio

        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) { this->onQuit(); return; }

        // El velo cuelga de la escena y no del PlayLayer: asi sigue animandose
        // aunque el PlayLayer tenga sus acciones detenidas, como ocurre en pausa.
        auto veil = CCLayerColor::create({0, 0, 0, 0});
        veil->setContentSize(CCDirector::get()->getWinSize());
        scene->addChild(veil, 9999);

        veil->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCCallFunc::create(this, callfunc_selector(SheepPlayLayer::fadeOutLevelMusic)),
            CCFadeTo::create(kQuitFade, 255),
            CCCallFunc::create(this, callfunc_selector(SheepPlayLayer::onQuitDelayed)),
            nullptr
        ));
    }

    // Arranca el fundido de la musica a la vez que el de la imagen. Se recorre
    // m_fmodMusic y no un canal fijo porque un nivel 2.2 con triggers de cancion
    // reparte sus pistas por varios canales, y ese mapa es el registro completo de
    // lo que suena.
    //
    // Nota: fadeOutMusic es un tween de volumen, no una parada: deja el canal a
    // cero y no lo restaura. Aqui es seguro porque onQuit para despues toda la
    // musica y arranca la del menu con su propio volumen.
    void fadeOutLevelMusic() {
        auto fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;
        for (auto const& entry : fmod->m_fmodMusic) {
            fmod->fadeOutMusic(kQuitFade, entry.second.m_channelID);
        }
    }

    // Final de la secuencia de quitAfter, cuando la pantalla ya esta en negro.
    void onQuitDelayed() { this->onQuit(); }

    // Pide el salto al nivel 'id' a SheepLevelLauncher, que lo busca, lo descarga
    // si hace falta y lo lanza con fundido.
    void launchNow(int id) {
        // El schedule no se desprograma: lo necesita el watchdog de sheepTick por
        // si este lanzamiento no llega a ocurrir. Lo que evita el doble disparo es
        // m_swapping.
        m_fields->m_swapping = true;
        m_fields->m_swapWait = 0.f;
        log::info("[SheepGD] {}", sheep::trf("Log_JumpingTo", id));
        SheepLevelLauncher::play(id);
    }

    // Lo que toca hacer al morir, decidido desde sheepTick: lanzar el nivel que
    // esperaba a la muerte ("swap on death") o salir a la lista (kEndUntilDeath).
    void onDeathAction() {
        if (m_fields->m_pendingLevelId > 0) {
            // El id se queda donde esta: lo consume launchPending. Medio segundo
            // para que la explosion se vea entera; el fundido del lanzador viene
            // detras, asi que la pausa que se percibe es algo mayor.
            m_fields->m_swapping = true;
            m_fields->m_swapWait = 0.f;
            this->runAction(CCSequence::create(
                CCDelayTime::create(kDeathPause),
                CCCallFunc::create(this, callfunc_selector(SheepPlayLayer::launchPending)),
                nullptr
            ));
            return;
        }
        if (m_fields->m_exitOnDeath) {
            m_fields->m_exitOnDeath = false;
            log::info("[SheepGD] {}", sheep::tr("Log_EndExitAfterDeath"));
            quitAfter(kDeathPause);
        }
    }

    // Lanza el nivel que esperaba a la muerte, tras la pausa de onDeathAction.
    void launchPending() {
        int id = m_fields->m_pendingLevelId;
        m_fields->m_pendingLevelId = 0;
        if (id > 0) launchNow(id);   // vuelve a marcar m_swapping y reinicia el watchdog
    }

    // Muerte del jugador. Solo levanta la bandera m_deathPending; la decision se
    // toma en el siguiente tick (ver el comentario correspondiente en sheepTick).
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);

        if (!SheepQueueLayer::s_returnTicket) return;
        if (m_fields->m_swapping) return;
        if (m_fields->m_pendingLevelId > 0 || m_fields->m_exitOnDeath) {
            m_fields->m_deathPending = true;
        }
    }

    // Salida del nivel. Si la partida vino de la cola, en lugar de volver a donde
    // iria GD se vuelve a la escena del mod.
    //
    // La salida sigue tres reglas, aprendidas al romperlas:
    // 1. El original (PlayLayer::onQuit) se llama en este mismo frame, nunca
    //    aplazado. Se probo a montar un velo y llamarlo 0,35 s despues, y se
    //    rompia el audio: GD llama a onQuit desde el menu de pausa contando con
    //    desmontarse en ese frame, incluida la contabilidad del motor de audio,
    //    que en pausa esta en un estado propio. El sintoma era el efecto de salida
    //    sonando al entrar al nivel siguiente, y el menu y los niveles posteriores
    //    sin sonido.
    // 2. El replaceScene va sin transicion (ver el comentario del final).
    // 3. PlayLayer::onQuit no toca el audio: eso lo hace el PauseLayer. Las salidas
    //    automaticas, que no pasan por el menu de pausa, lo hacen a mano.
    //
    // De las reglas 1 y 2 sale que la salida por el menu de pausa no puede llevar
    // fundido de salida: solo lleva el de entrada, que pone la propia escena del
    // mod (scene(true)). Las salidas automaticas si lo llevan, con quitAfter,
    // porque ahi la decision es del mod y GD todavia no ha llamado a nada suyo.
        void onQuit() {
        if (m_fields->m_quitting) return;   // doble clic en el boton de salir

        if (!SheepQueueLayer::s_returnTicket) {
            PlayLayer::onQuit();            // partida normal: no es asunto del mod
            return;
        }
        m_fields->m_quitting            = true;
        SheepQueueLayer::s_returnTicket = false;
        g_sheepPaused                   = false;
        sheep::inPlaceholder()          = false;

        // El original, en su frame (regla 1).
        PlayLayer::onQuit();

        // Regla 3: lo que PlayLayer::onQuit no hace aunque lo parezca, el audio.
        // Parar la musica del nivel y arrancar la del menu es cosa del PauseLayer,
        // no de esta funcion. Por eso salir con el boton sonaba bien y la salida
        // automatica dejaba el nivel sonando (o en silencio, si venia de una
        // muerte). Solo en las salidas automaticas, para no duplicarlo en la manual.
        if (m_fields->m_autoQuit) {
            if (auto fmod = FMODAudioEngine::sharedEngine()) {
                // 1. Por la via de GD: para la musica y limpia su registro.
                fmod->stopAllMusic(true);

                // 2. Red de seguridad por debajo, igual que en el salto de nivel:
                //    el grupo maestro de FMOD es el padre de todos los canales y
                //    no depende de que GD tenga la pista registrada como musica.
                if (fmod->m_system) {
                    FMOD::ChannelGroup* master = nullptr;
                    if (fmod->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) {
                        master->stop();
                    }
                }
            }
            // Y el fundido de entrada de la musica del menu. Va despues del stop,
            // asi que arranca con su propio volumen y anula cualquier fundido de
            // salida que hubiera quedado a medias en esos canales.
            GameManager::sharedState()->fadeInMenuMusic();
        }

        // Regla 2: el replaceScene, sin transicion. El original acaba de dejar
        // programado su propio cambio de escena. Con una escena normal, el nuestro
        // se aplica al final de este frame y llega primero; con una
        // CCTransitionScene de por medio, el cambio pendiente de GD se aplica
        // durante la transicion y se acaba en el menu principal. Comprobado.
        CCDirector::get()->replaceScene(SheepQueueLayer::scene(true));
    }

    // En pausa el schedule del nodo esta parado, asi que un salto pedido con el
    // teclado no se aplicaria hasta reanudar y se veria como un tiron sin causa.
    // Por eso el hook del teclado ignora Q y E mientras g_sheepPaused este puesto.
    void pauseGame(bool unfocused) {
        g_sheepPaused = true;
        PlayLayer::pauseGame(unfocused);
    }

    void resume() {
        g_sheepPaused = false;
        PlayLayer::resume();
    }
};

// El cubo de Sheepy en la partida. Solo cambia el dibujo: el hitbox lo calcula GD
// a partir del jugador, no del sprite, asi que sigue siendo el cuadrado de siempre.
//
// El montaje de las capas y el apagado de las de GD estan en SheepIcon.hpp,
// porque el taller de iconos hace lo mismo sobre un SimplePlayer. Aqui queda solo
// lo que es de la partida: cuando se ve y de donde salen los colores.
class $modify(SheepPlayerObject, PlayerObject) {
    struct Fields {
        sheep::icon::Layers m_sheepy;

        // Indica si el nivel ya se completo. Hace falta una bandera propia porque
        // al completar no hay un campo de GD que lo diga: al morir queda m_isDead y
        // la condicion 'ver' de update se apaga sola, pero al completar esCubo
        // sigue siendo cierto y m_isHidden y m_isDead siguen en false.
        bool m_completed = false;
    };

    // Visibilidad y color de Sheepy, en cada frame.
    //
    // La visibilidad se decide aqui y no a partir de la del cubo de GD: el juego
    // reutiliza m_iconSprite para la bola, la onda y el swing (les cambia el
    // frame) y lo aparta para el robot y la arana sin tocar su visibilidad.
    void update(float dt) {
        PlayerObject::update(dt);

        // El montaje va aqui, y no en un hook de updatePlayerFrame como antes.
        //
        // Ese hook dejo de ejecutarse con una actualizacion de los bindings:
        // compilaba, enlazaba y no se disparaba nunca. Se comprobo con una sonda el
        // 2026-09-18 (la de update salia y la de updatePlayerFrame no aparecia ni
        // una vez), y por eso las capas no llegaban a montarse.
        //
        // Hacerlo por frame es ademas lo que ya hacian el taller y el selector de
        // colores: decidir en cada frame en vez de enumerar los sitios donde GD
        // refresca el icono, que es como no se olvida ninguno. Cuesta un puntero y
        // una comparacion, porque build() sale en su primera linea en cuanto las
        // capas estan puestas.
        applySheepy();

        auto& L = m_fields->m_sheepy;
        if (!L.body) return;

        // Sheepy es el cubo, y el cubo tambien se dibuja dentro de la nave y del
        // ovni: esos dos no cuentan como vehiculo aqui.
        bool esCubo = !m_isBall && !m_isDart && !m_isRobot && !m_isSpider && !m_isSwing;
        bool ver    = esCubo && !m_isHidden && !m_isDead && !m_fields->m_completed;

        sheep::icon::apply(L, ver, m_hasGlow,
            m_iconSprite          ? m_iconSprite->getColor()          : ccColor3B{255, 255, 255},
            m_iconSpriteSecondary ? m_iconSpriteSecondary->getColor() : ccColor3B{255, 255, 255},
            m_iconGlow            ? m_iconGlow->getColor()            : ccColor3B{255, 255, 255});

        // Las capas de GD se apagan en cada frame mientras Sheepy este en
        // pantalla, no una sola vez: updatePlayerGlow corre despues y por su
        // cuenta, y el brillo del cubo real reaparecia superpuesto.
        //
        // Va dentro del 'if (ver)' a proposito: en bola, onda y swing esas mismas
        // capas son las de la bola, la onda y el swing.
        if (ver) blankGameLayers();
    }

    // Al explotar, GD esconde al jugador por una via que no pasa por estos nodos.
    // Se esconde a Sheepy explicitamente para que no quede flotando sobre la
    // explosion. Despues lo mantiene oculto m_isDead, en la condicion de update.
    void playerDestroyed(bool noEffects) {
        auto& L = m_fields->m_sheepy;
        if (L.body) L.body->setVisible(false);
        if (L.face) L.face->setVisible(false);
        if (L.bord) L.bord->setVisible(false);
        PlayerObject::playerDestroyed(noEffects);
    }

    // El otro caso: completar el nivel, que GD resuelve por otra via.
    //
    // Se esconde aqui y ademas se marca la bandera. Lo primero evita el parpadeo de
    // un frame; lo segundo es lo que lo mantiene oculto, porque sin la bandera el
    // siguiente update volveria a mostrar a Sheepy.
    void playCompleteEffect(bool noEffects, bool instant) {
        auto& L = m_fields->m_sheepy;
        m_fields->m_completed = true;
        if (L.body) L.body->setVisible(false);
        if (L.face) L.face->setVisible(false);
        if (L.bord) L.bord->setVisible(false);

        PlayerObject::playCompleteEffect(noEffects, instant);
    }

    // Un reinicio que no recrea el PlayerObject, como el Retry del menu de fin de
    // nivel, tiene que devolver a Sheepy. GD pasa por aqui en ese caso.
    void resetObject() {
        m_fields->m_completed = false;
        PlayerObject::resetObject();
    }

    // Monta las capas de Sheepy sobre m_iconSprite si los ajustes lo piden. No
    // hace nada si ya estan montadas.
    void applySheepy() {
        if (!sheep::sheepyEnabled()) return;

        // "Only in requests": el billete esta puesto mientras la partida venga de la
        // cola del mod, y se consume al salir.
        if (sheep::sheepyOnlyRequests() && !SheepQueueLayer::s_returnTicket) return;

        if (!m_iconSprite) return;
        if (!sheep::icon::build(m_iconSprite, m_fields->m_sheepy)) return;

        // Las capas de GD no se apagan aqui, y es la clave de este metodo.
        //
        // Antes si se apagaban, porque esto corria desde updatePlayerFrame (solo al
        // cambiar de icono) y hacia falta para que el primer frame no mostrase el
        // cubo real. Desde que el montaje vive dentro de update (cada frame), ese
        // apagado sin condicion se aplicaba tambien en bola, onda y swing, donde
        // m_iconSprite es el sprite que dibuja GD, y los dejaba invisibles.
        //
        // El apagado correcto es el de update, detras de 'if (ver)'. Y el hueco que
        // cubria esta llamada ya no existe: applySheepy se ejecuta en update, en el
        // mismo frame y antes de ese if.
    }

    // Apaga las cuatro capas del cubo de GD vaciandoles el rectangulo de textura
    // (ver sheep::icon::blank para el porque de no usar setVisible).
    void blankGameLayers() {
        sheep::icon::blank(m_iconSprite);
        sheep::icon::blank(m_iconSpriteSecondary);
        sheep::icon::blank(m_iconSpriteWhitener);
        sheep::icon::blank(m_iconGlow);
    }
};

// Sheepy en el taller de iconos.
//
// Aqui no hay PlayerObject: la vista previa es un SimplePlayer, con sus propias
// capas (m_firstLayer / m_secondLayer / m_detailSprite / m_outlineSprite) y sin
// banderas de modo de juego como m_isBall. Lo unico que se reutiliza es
// SheepIcon.hpp.
//
// Solo aparece con "Only in requests" apagado, y es lo correcto: el taller no es
// una peticion, asi que quien tenga esa opcion puesta espera ver su cubo de
// siempre.
class $modify(SheepGarageLayer, GJGarageLayer) {
    struct Fields {
        sheep::icon::Layers m_sheepy;
    };

    bool init() {
        if (!GJGarageLayer::init()) return false;

        // Por frame, como en el PlayerObject y por la misma razon: GD refresca esta
        // vista previa desde updatePlayerColors, selectTab, onSelect y las flechas
        // de pagina. Decidir cada frame evita enumerar esos sitios y olvidar alguno,
        // que en directo se veria como una oveja que no cambia de color.
        this->schedule(schedule_selector(SheepGarageLayer::sheepyTick), 0.f);
        return true;
    }

    // Muestra u oculta a Sheepy en la vista previa y le copia los colores.
    void sheepyTick(float) {
        auto sp = m_playerObject;
        if (!sp || !sp->m_firstLayer) return;

        bool ver = sheep::sheepyEnabled()
                && !sheep::sheepyOnlyRequests()
                && m_iconType == IconType::Cube;

        if (!ver) {
            // Se oculta a Sheepy, pero no se tocan las capas de GD: al cambiar de
            // pestana el juego les pone el frame nuevo y se recuperan solas.
            sheep::icon::apply(m_fields->m_sheepy, false, false,
                               {255, 255, 255}, {255, 255, 255}, {255, 255, 255});
            return;
        }

        if (!sheep::icon::build(sp->m_firstLayer, m_fields->m_sheepy)) return;

        sheep::icon::apply(m_fields->m_sheepy, true, sp->m_hasGlowOutline,
            sp->m_firstLayer->getColor(),
            sp->m_secondLayer   ? sp->m_secondLayer->getColor()   : ccColor3B{255, 255, 255},
            sp->m_outlineSprite ? sp->m_outlineSprite->getColor() : ccColor3B{255, 255, 255});

        sheep::icon::blank(sp->m_firstLayer);
        sheep::icon::blank(sp->m_secondLayer);
        sheep::icon::blank(sp->m_detailSprite);
        sheep::icon::blank(sp->m_outlineSprite);
    }
};

// Sheepy en el popup del selector de colores, que no es la escena del taller: es
// una FLAlertLayer aparte con su propia fila de vistas previas en m_playerObjects.
// Por eso el hook de GJGarageLayer no le llegaba.
//
// El cubo se identifica por posicion (el indice 0 de la fila), porque
// SimplePlayer no guarda su IconType y no hay forma de preguntarselo. Si GD
// reordena esa fila algun dia, hay que cambiar kCubeIndex.
class $modify(SheepColorPage, CharacterColorPage) {
    struct Fields {
        sheep::icon::Layers m_sheepy;
    };

    static constexpr unsigned int kCubeIndex = 0;

    bool init() {
        if (!CharacterColorPage::init()) return false;
        this->schedule(schedule_selector(SheepColorPage::sheepyTick), 0.f);
        return true;
    }

    // Igual que SheepGarageLayer::sheepyTick, sobre la vista previa del cubo.
    void sheepyTick(float) {
        if (!m_playerObjects || m_playerObjects->count() <= kCubeIndex) return;

        auto sp = static_cast<SimplePlayer*>(m_playerObjects->objectAtIndex(kCubeIndex));
        if (!sp || !sp->m_firstLayer) return;

        // Mismo criterio que en el taller: "Only in requests" lo apaga, porque esto
        // tampoco es una peticion. Aqui no hay pestanas que mirar: la fila siempre
        // muestra el cubo en su sitio.
        bool ver = sheep::sheepyEnabled() && !sheep::sheepyOnlyRequests();

        if (!ver) {
            sheep::icon::apply(m_fields->m_sheepy, false, false,
                               {255, 255, 255}, {255, 255, 255}, {255, 255, 255});
            return;
        }

        if (!sheep::icon::build(sp->m_firstLayer, m_fields->m_sheepy)) return;

        sheep::icon::apply(m_fields->m_sheepy, true, sp->m_hasGlowOutline,
            sp->m_firstLayer->getColor(),
            sp->m_secondLayer   ? sp->m_secondLayer->getColor()   : ccColor3B{255, 255, 255},
            sp->m_outlineSprite ? sp->m_outlineSprite->getColor() : ccColor3B{255, 255, 255});

        sheep::icon::blank(sp->m_firstLayer);
        sheep::icon::blank(sp->m_secondLayer);
        sheep::icon::blank(sp->m_detailSprite);
        sheep::icon::blank(sp->m_outlineSprite);
    }
};

// Teclas Q y E: cambiar de nivel sin salir a la lista.
//
// PlayLayer no puede capturar el teclado (no declara nada de teclado en sus
// bindings), asi que va por el despachador de cocos. Nota sobre la firma: lleva
// un double al final, y equivocarse ahi no da error de compilacion ni de enlace:
// simplemente el hook no se engancha.
//
// La escena del mod escucha el teclado por su cuenta; este hook solo actua dentro
// de la partida.
class $modify(SheepKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double timing) {
        // Solo dentro de una partida lanzada desde la cola, que es justo lo que
        // indica el billete de vuelta. Sin esta guarda, Q y E quedarian capturadas
        // en las partidas normales y en el editor.
        //
        // isKeyRepeat queda fuera: mantener la tecla pulsada no debe vaciar la cola.
        // Devolver true consume la tecla, para que no llegue a nadie mas.
        if (isKeyDown && !isKeyRepeat && !g_sheepPaused && SheepQueueLayer::s_returnTicket) {
            if (key == KEY_E) { sheepQueueStep(true);  return true; }
            if (key == KEY_Q) { sheepQueueStep(false); return true; }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, timing);
    }
};