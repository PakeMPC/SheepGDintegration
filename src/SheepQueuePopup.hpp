#pragma once
#include <Geode/Geode.hpp>
#include "SheepBouncer.hpp"
#include "SheepData.hpp"
#include "SheepNowPlayingCard.hpp"
#include "SheepQueuePanel.hpp"

using namespace geode::prelude;

// La escena del mod: la tarjeta del nivel actual, la lista de la cola, las flechas
// de anterior y siguiente, los ajustes y los dos Sheepys de las esquinas. Se abre
// desde el boton de la oveja del menu principal y es a donde se vuelve al salir de
// un nivel lanzado desde la cola.
//
// Es delegado de GameLevelManager para buscar y descargar los niveles de View y
// Play. Ese hueco lo comparte con otros tres consumidores (el lanzador, el
// precargador y el buscador del placeholder); ver m_searching y m_downloading.
class SheepQueueLayer :
    public cocos2d::CCLayer,
    public LevelManagerDelegate,
    public LevelDownloadDelegate
{
public:
    // fadeIn = true al volver de un nivel: la escena entra con un velo negro.
    static SheepQueueLayer* create(bool fadeIn = false);
    static cocos2d::CCScene* scene(bool fadeIn = false);

    // Billete de vuelta: indica que la partida en curso se lanzo desde la cola.
    // Lo marca launchWaitedLevel justo antes de lanzar un nivel; lo consultan los
    // hooks de main.cpp (el vigia de la cola, las teclas Q/E y la opcion "Only in
    // requests" de Sheepy); lo consume PlayLayer::onQuit al volver aqui, e init lo
    // limpia por si acaso.
    //
    // Es static porque esta capa no sobrevive al viaje: GD la destruye al
    // reemplazar la escena, asi que el estado tiene que vivir fuera del objeto.
    static bool s_returnTicket;

    // --- LevelManagerDelegate ---
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int type) override;
    void loadLevelsFailed(char const* key) override;
    void loadLevelsFailed(char const* key, int type) override;

    // --- LevelDownloadDelegate ---
    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int code) override;

protected:
    ~SheepQueueLayer() override;

    bool init() override;
    void keyBackClicked() override;

    // Construccion de la interfaz, desde init.
    void createTopPanel();
    void createBottomPanel();
    void createNavArrows();

    // Datos: enlazar el archivo, repintar desde SheepData y guardar tras mutar.
    void promptForDataPath();
    void applyData();          // sin parametro 'force': el filtro de cambios lo hace el servicio
    void paintCard();          // la parte de applyData que pinta la tarjeta
    void persistAndRefresh();

    // Botones de la escena y final del velo de entrada.
    void onBack(CCObject*);
    void onPrevLevel(CCObject*);
    void onNextLevel(CCObject*);
    void onVeilDone(cocos2d::CCNode* veil);

    // Acciones de GD: buscar, abrir la pagina de un nivel o lanzarlo.
    void requestLevel(int levelId, bool playAfter);
    void openLevelInfo(GJGameLevel* level);
    void clearDelegates();
    void showLoading(bool visible);

    // Espera de la musica antes de lanzar un nivel con Play.
    void audioWaitTick(float dt);
    void launchWaitedLevel();
    void stopAudioWait();
    int  fallbackForLevel(int levelId);

    // Deslizamiento de la tarjeta al cambiar de nivel con las flechas.
    void slideCard(bool toLeft);
    void onCardSlideMid();
    void onCardSlideDone();

    void onSettings(CCObject*);

    // Los Sheepys de las esquinas y el tick de la escena (que tambien vigila la
    // tarjeta del placeholder).
    void createBouncers();
    void sceneTick(float dt);
    void bounce(bool player2);

    // El fork de RobTop incluye CCKeyboardDelegate y CCMouseDelegate en CCLayer, asi
    // que la escena puede escuchar por su cuenta y no hace falta tocar el hook
    // global del teclado (el de las teclas Q/E de la partida).
    void keyDown(cocos2d::enumKeyCodes key, double timing) override;
    void rightKeyDown() override;
    bool hasPopupOpen();
    void ccTouchesBegan(cocos2d::CCSet* touches, cocos2d::CCEvent* event) override;

private:
    // Atajo: los datos no son de la escena, viven en el servicio global.
    QueueData& data() { return SheepData::get()->data(); }

    int m_subId = 0;   // id de suscripcion a SheepData, para darse de baja al destruirse

    SheepNowPlayingCard* m_card = nullptr;
    SheepQueuePanel* m_queuePanel = nullptr;

    SheepBouncer* m_bounceP1 = nullptr;   // izquierda, colores del jugador
    SheepBouncer* m_bounceP2 = nullptr;   // derecha, los dos colores al reves

    LoadingCircle* m_loading = nullptr;
    int m_pendingLevelId = 0;   // nivel que se esta buscando o descargando

    // Indican si hay una busqueda o una descarga propia en curso. Hacen falta
    // porque el hueco del delegado es unico y compartido entre cuatro
    // consumidores, y los callbacks de fallo no traen el id del nivel sino el
    // codigo de error: es lo unico que se puede comprobar para saber si el fallo
    // era propio.
    bool m_searching   = false;
    bool m_downloading = false;

    // El nivel descargado esperando a que llegue su musica. Se retiene porque la
    // espera dura varios frames y el CCArray de resultados no tiene por que
    // sobrevivir tanto.
    GJGameLevel* m_waitLevel = nullptr;
    int   m_waitFallbackSongId = 0;   // cancion del JSON por si el nivel no trae la suya
    float m_audioWaited = 0.f;        // segundos de espera acumulados

    bool m_playAfterFetch = false;   // tras la busqueda: true = Play, false = View
    bool m_fadeIn = false;           // poner el velo de entrada en init

    bool m_showingPlaceholder = false;   // la tarjeta muestra el placeholder

    // Hay una transicion en curso (deslizamiento de la tarjeta, borrado o Play de
    // una fila). Mientras tanto applyData no repinta: lo hace onCardSlideDone.
    bool  m_cardSliding     = false;
    float m_pendingSlideInX = 0.f;   // X desde la que vuelve a entrar la tarjeta
};