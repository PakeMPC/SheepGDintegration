#pragma once
#include <Geode/Geode.hpp>
#include "SheepI18n.hpp"
#include "SheepAssets.hpp"

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Las tres capas del cubo de Sheepy, y las dos reglas que las gobiernan: como se
// montan (build, apply) y como se apagan las del juego (blank).
//
// Vive aparte porque hay varios sitios que dibujan a Sheepy y no se parecen en
// nada: el PlayerObject de la partida, el SimplePlayer del taller de iconos y del
// selector de colores (los tres en main.cpp), y los dos Sheepys saltarines de la
// escena (SheepBouncer.hpp). Lo unico que comparten es esto, y con el codigo
// duplicado la siguiente correccion se habria aplicado solo a uno de ellos.
//
// Lo que no vive aqui es cuando hay que mostrarlo: eso depende del sitio (los modos
// de juego en la partida, la pestana abierta en el taller) y lo decide cada uno.
// ---------------------------------------------------------------------------
namespace sheep::icon {

    // Escalas de las capas. El contorno (el glow) vino en un lienzo de 150 px y las
    // otras dos en uno de 137, pero las dos escalas acabaron en 1.0: el arte ya esta
    // exportado al tamano correcto. Si algun dia se reexporta, son estos dos numeros.
    inline constexpr float kScale     = 1.f;
    inline constexpr float kBordScale = kScale;

    // Las tres capas de un Sheepy montado. Los punteros no retienen los sprites: los
    // mantiene vivos su padre (el 'host' de build). Por eso no se leen sin saber
    // antes que siguen colgados de el (ver build).
    struct Layers {
        cocos2d::CCNode*   host = nullptr;   // donde se montaron; solo se compara
        cocos2d::CCSprite* bord = nullptr;   // z 9  -> el glow
        cocos2d::CCSprite* body = nullptr;   // z 11 -> color 1 (lleva el contorno negro dentro)
        cocos2d::CCSprite* face = nullptr;   // z 10 -> hocico y orejas, color 2
    };

    // Apaga un sprite del juego vaciandole el rectangulo de textura, nunca con
    // setVisible. Esa es toda la diferencia: el rectangulo lo reescribe GD por su
    // cuenta en cuanto le asigna otro frame (al cambiar de modo, por ejemplo), asi
    // que se recupera solo. Un setVisible(false) lo pone el mod y el juego no lo
    // deshace nunca, porque no sabe que esta apagado: el sprite se quedaria
    // invisible en modos donde GD lo necesita.
    inline void blank(cocos2d::CCSprite* s) {
        if (s) s->setTextureRect(cocos2d::CCRectMake(0.f, 0.f, 0.f, 0.f));
    }

    // Monta las capas dentro de 'host', o las vuelve a montar si ya no estan en el.
    // Si ya estaban montadas en ese host, no hace nada: por eso se puede llamar en
    // cada frame. Devuelve false si falta el arte del cuerpo y no hay nada que hacer.
    //
    // Recibe CCNode y no CCSprite: el PlayerObject y el taller cuelgan las capas de
    // un sprite del juego, pero los Sheepys saltarines las cuelgan de un nodo vacio,
    // que es el que se mueve.
    //
    // Para saber si ya estaban se pregunta a la lista de hijos del host, que es el
    // de esta llamada y esta vivo, y no a las capas: si el juego hubiera quitado una,
    // su puntero ya no seria valido y leerlo seria usar memoria liberada.
    //
    // Si el host cambio, los punteros viejos no se tocan (el host anterior puede no
    // existir ya) y se monta de cero. Si ese host anterior siguiera vivo, sus capas
    // se quedarian en el: hoy ningun sitio cambia de host mientras vive su Layers.
    inline bool build(cocos2d::CCNode* host, Layers& L) {
        if (!host) return false;

        if (L.host == host) {
            auto hijos = host->getChildren();
            auto cuelga = [&](cocos2d::CCSprite* s) {
                return s && hijos && hijos->containsObject(s);
            };
            if (cuelga(L.body)) return true;   // ya estaban

            // Falta el cuerpo: se quitan las otras capas que sigan en el host, para
            // no duplicarlas al montar de nuevo.
            if (cuelga(L.bord)) L.bord->removeFromParent();
            if (cuelga(L.face)) L.face->removeFromParent();
        }

        L = Layers{};
        L.host = host;

        // Crea una capa, la configura y la cuelga de host. nullptr si falta el PNG
        // (createOwn ya lo avisa en el log).
        auto crear = [&](char const* archivo, int z, float escala) -> cocos2d::CCSprite* {
            auto s = createOwn(archivo);
            if (!s) return nullptr;
            // El tinte se aplica capa por capa, asi que el color del padre no debe
            // llegarles: si se propagase, el color se multiplicaria dos veces y
            // saldria mas oscuro.
            s->setCascadeColorEnabled(false);
            s->setPosition({0.f, 0.f});
            s->setScale(escala);
            host->addChild(s, z);
            return s;
        };

        // De atras hacia delante: glow, cuerpo, hocico y orejas.
        L.bord = crear("Sheepy_Player_bord.png"_spr, 9,  kBordScale);
        L.body = crear("Sheepy_Player.png"_spr,      11, kScale);
        L.face = crear("Sheepy_Player_2.png"_spr,    10, kScale);

        return L.body != nullptr;   // sin la capa principal no hay Sheepy
    }

    // Visibilidad y tinte de las tres capas. Los colores no se deciden aqui: los
    // pasa quien llama, copiados de las capas del juego (que ya vienen tenidas con lo
    // que haya decidido GD) o leidos del GameManager.
    //
    // Los tres PNG son blancos sobre transparente, porque setColor multiplica:
    // blanco por un color da ese color, y el negro del contorno se queda negro.
    inline void apply(Layers const& L, bool visible, bool glow,
                      cocos2d::ccColor3B c1, cocos2d::ccColor3B c2,
                      cocos2d::ccColor3B cGlow) {
        if (L.body) { L.body->setVisible(visible); L.body->setColor(c1); }
        if (L.face) { L.face->setVisible(visible); L.face->setColor(c2); }

        // La capa _bord es el glow, no el contorno: el contorno negro ya va pintado
        // dentro de Sheepy_Player.png. Por eso solo aparece si el jugador tiene el
        // brillo activado.
        if (L.bord) { L.bord->setVisible(visible && glow); L.bord->setColor(cGlow); }
    }

} // namespace sheep::icon