#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

// Los ajustes del mod, en un popup propio que se abre con el engranaje de la
// escena: reloj de la partida, cubo de Sheepy, que hacer al acabarse la cola y
// "Swap on death". Los valores se guardan en Mod::getSavedValue con las claves
// de SheepData.hpp, en cuanto se cambian; el ID del placeholder, al cerrar.
//
// Se dibuja a mano en vez de usar los `settings` del mod.json de Geode, a
// proposito: asi no hay que salir de la escena, ir a la lista de mods y buscar
// este, que en mitad de un directo es justo lo que se quiere evitar.
class SheepSettingsPopup : public geode::Popup {
public:
    static SheepSettingsPopup* create();

protected:
    bool setup();     // monta el contenido; la llama create despues de init
    void refresh();   // pone cada control en el estado guardado

    // Al cerrar (boton, Escape) guarda el ID del placeholder y cierra.
    void onClose(cocos2d::CCObject* sender) override;

    // Interruptores de las filas 1 y 2.
    void onToggleIngameTimer(CCObject*);
    void onToggleLastTen(CCObject*);
    void onToggleSheepyIcon(CCObject*);
    void onToggleSheepyOnlyRequests(CCObject*);

    // Flechas del selector de fin de cola, y la casilla de "Swap on death".
    void onEndPrev(CCObject*);
    void onEndNext(CCObject*);
    void onToggleSwapOnDeath(CCObject*);

private:
    void stepEndOfQueue(int delta);

    // Cada casilla se guarda por su marca de "activado" (m_chk*On), que es lo que
    // se muestra u oculta. Las de la fila 2 guardan ademas la etiqueta y el boton,
    // porque desaparecen enteras cuando su opcion de la fila 1 esta apagada.
    cocos2d::CCSprite* m_chkTimerOn   = nullptr;   // la marca de la casilla
    cocos2d::CCSprite* m_chkLastOn    = nullptr;
    cocos2d::CCNode*   m_lastTenLabel = nullptr;   // el bloque de abajo, entero
    cocos2d::CCNode*   m_lastTenItem  = nullptr;

    cocos2d::CCSprite* m_chkSheepyOn  = nullptr;
    cocos2d::CCSprite* m_chkOnlyReqOn = nullptr;
    cocos2d::CCNode*   m_onlyReqLabel = nullptr;
    cocos2d::CCNode*   m_onlyReqItem  = nullptr;

    // Fin de cola y momento del cambio de nivel ("Swap on death").
    cocos2d::CCLabelBMFont* m_endLabel     = nullptr;   // el modo elegido, en texto
    geode::TextInput*       m_placeholder  = nullptr;   // solo visible en modo placeholder
    cocos2d::CCSprite*      m_chkSwapDeath = nullptr;
};