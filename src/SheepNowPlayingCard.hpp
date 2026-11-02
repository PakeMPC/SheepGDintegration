#pragma once
#include <Geode/Geode.hpp>
#include "QueueData.hpp"

using namespace geode::prelude;

// Tarjeta grande "Now Playing" de la parte de arriba de la escena: el nivel que
// se esta jugando, con su cara de dificultad, sus datos, la miniatura, el reloj
// del auto-skip y los botones Play, View, papelera y dado.
//
// No conoce el JSON ni la cola: recibe un LevelModel con setLevel y avisa de los
// botones por callbacks, que conecta la escena. Lo que si consulta por su cuenta
// es el progreso de orbes del jugador (al juego) y la miniatura (a la cache o a
// la red).
class SheepNowPlayingCard : public cocos2d::CCNode {
public:
    static SheepNowPlayingCard* create(cocos2d::CCSize const& size);

    // Muestra un nivel, o el estado vacio si no es valido (centinela WAITING).
    void setLevel(LevelModel const& level);

    // Modo placeholder: el nivel que se muestra no es una peticion. Hay que
    // llamarlo antes de setLevel, porque refreshTexts lo consulta.
    void setPlaceholder(bool on);

    // Callbacks de los botones; los conecta la escena. Reciben una referencia a
    // m_level, la copia del nivel que guarda la tarjeta: si el callback acaba
    // llamando a setLevel, esa referencia pasa a ver el nivel nuevo, asi que hay
    // que leer lo necesario antes.
    std::function<void(LevelModel const&)> onPlay;
    std::function<void(LevelModel const&)> onView;
    std::function<void(LevelModel const&)> onDelete;
    std::function<void()> onRandom;

protected:
    bool init(cocos2d::CCSize const& size);

    // Construccion, una sola vez (desde init).
    void buildBackground();
    void buildLeftColumn();
    void buildInfoColumn();
    void buildButtons();

    // Actualizacion de cada parte al cambiar de nivel (desde setLevel).
    void refreshFace();
    void refreshTexts();
    void refreshStatsRow();
    void refreshThumbnail();
    void refreshRatingRow();
    void setEmptyState(bool empty);
    void updateTimer(float dt);   // tambien la llama el schedule cada 0,5 s

    // Manejadores de los botones (menu_selector necesita funciones miembro).
    void handlePlay(CCObject*);
    void handleView(CCObject*);
    void handleDelete(CCObject*);
    void handleRandom(CCObject*);

private:
    cocos2d::CCSize m_size;
    LevelModel m_level;        // copia del nivel que se esta mostrando
    bool m_hasLevel = false;   // m_level es valido (no es el centinela WAITING)

    // Fondo / miniatura
    cocos2d::CCNode*   m_thumbHolder = nullptr;
    cocos2d::CCSprite* m_thumbSprite = nullptr;

    // Columna izquierda
    cocos2d::CCNode* m_faceHolder = nullptr;
    cocos2d::CCLabelBMFont* m_starsLabel = nullptr;
    cocos2d::CCSprite* m_starsIcon = nullptr;

    // Columna central
    cocos2d::CCLabelBMFont* m_titleLabel   = nullptr;
    cocos2d::CCLabelBMFont* m_authorLabel  = nullptr;
    cocos2d::CCNode*        m_ratingRow    = nullptr;
    cocos2d::CCLabelBMFont* m_sentByLabel  = nullptr;
    cocos2d::CCLabelBMFont* m_timerLabel = nullptr;
    cocos2d::CCNode* m_platformHolder      = nullptr;
    cocos2d::CCNode* m_statsRow            = nullptr;

    // Estado vacio
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    // Botones
    cocos2d::CCMenu* m_buttonMenu = nullptr;

    CCMenuItemSpriteExtra* m_deleteBtn = nullptr;   // se ocultan en modo placeholder
    CCMenuItemSpriteExtra* m_randomBtn = nullptr;
    bool m_placeholder = false;

    // ID del nivel cuya miniatura ya se pidio a la red, para no pedirla dos veces.
    // La descarga retiene la tarjeta (Ref) hasta que termina, asi que su respuesta
    // nunca llega a una tarjeta ya liberada.
    std::string m_thumbRequestedFor;
};