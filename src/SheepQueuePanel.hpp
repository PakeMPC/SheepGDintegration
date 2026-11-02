#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include "QueueData.hpp"

using namespace geode::prelude;

// La lista "Next levels" de la parte de abajo de la escena: la cola, una fila por
// nivel, dentro de un ScrollLayer con marco propio (barras, rieles y esquinas)
// en lugar del GJListLayer del juego.
//
// Como la tarjeta, no conoce el JSON: recibe la cola con setQueue y avisa de los
// botones de cada fila por callbacks, que conecta la escena. Las animaciones de
// entrada y salida de filas las pide la escena antes de cambiar los datos.
class SheepQueuePanel : public cocos2d::CCNode {
public:
    static SheepQueuePanel* create(cocos2d::CCSize const& listSize);

    // Vacia la lista y muestra un texto centrado (cargando, cola vacia, sin archivo).
    void showMessage(std::string const& text);
    // Reconstruye todas las filas con la cola nueva, conservando el scroll.
    void setQueue(std::vector<LevelModel> const& queue);

    // Animaciones previas a un cambio de datos. La lista se reconstruye despues
    // con setQueue; devuelven false si no hay nada que animar.
    bool animateRemoval(size_t index);
    bool animateInsertion(LevelModel const& level);

    // Callbacks por fila. Mandan el indice ademas del nivel porque la escena lo
    // necesita para animar la fila correcta.
    //
    // El nivel va por valor, no por referencia constante, y es a proposito: estos
    // callbacks acaban llamando a persistAndRefresh, que reescribe m_queue entero.
    // Una referencia a m_queue[i] sobrevive a esa reescritura apuntando al mismo
    // sitio, pero con otro nivel dentro: el siguiente de la cola.
    std::function<void(size_t, LevelModel)> onCellPlay;
    std::function<void(size_t, LevelModel)> onCellView;
    std::function<void(size_t, LevelModel)> onCellDelete;

protected:
    bool init(cocos2d::CCSize const& listSize);

    void buildFrame();                    // marco y scroll, una sola vez
    void layoutContent(size_t count);     // alto del lienzo segun el numero de filas
    cocos2d::CCNode* buildCell(LevelModel const& level, cocos2d::CCSize const& size, size_t index);
    void attachThumb(cocos2d::CCNode* holder, cocos2d::CCSize const& size, LevelModel const& level);

    // Manejadores de los botones de las filas; el indice va en el tag del boton.
    void handleCellPlay(CCObject* sender);
    void handleCellView(CCObject* sender);
    void handleCellDelete(CCObject* sender);

private:
    cocos2d::CCSize m_listSize;               // tamano de la ventana visible de la lista
    geode::ScrollLayer* m_scroll = nullptr;
    std::vector<cocos2d::CCNode*> m_cells;    // filas en pantalla, de arriba abajo

    // Copia de la cola: los botones solo llevan el indice en su tag, asi que el
    // panel necesita poder traducir indice -> nivel.
    std::vector<LevelModel> m_queue;
};