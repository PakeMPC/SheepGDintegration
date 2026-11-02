#pragma once
#include <Geode/Geode.hpp>
#include "SheepIcon.hpp"
#include "SheepI18n.hpp"

using namespace geode::prelude;

// Un Sheepy que da un brinco cuando se pulsa la tecla de su jugador. Es decorativo:
// la escena de la lista pone dos, uno en cada esquina de abajo. No lee el JSON, no
// toca la cola y no depende del resto del mod.
//
// La escena crea los dos nodos siempre y se encarga de mostrarlos u ocultarlos
// segun el ajuste "Use Sheepy Icon", y de llamar a hop() con las teclas de cada
// jugador (jugador 1: espacio o clic izquierdo; jugador 2: flecha arriba o clic
// derecho).
//
// Estructura: un nodo con dos hijos hermanos, la sombra (m_shadow), que se queda en
// el suelo, y el cuerpo (m_body), que salta y lleva las tres capas de Sheepy.
class SheepBouncer : public cocos2d::CCNode {
public:
    // invert intercambia los dos colores del jugador. Es lo unico que distingue al
    // de la derecha del de la izquierda, asi que no hace falta arte aparte.
    static SheepBouncer* create(bool invert) {
        auto ret = new SheepBouncer();
        if (ret && ret->init(invert)) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    // Da un brinco. Si ya esta en el aire no hace nada: pulsar la tecla muchas
    // veces seguidas no acumula saltos.
    void hop() {
        if (m_jumping || !m_body) return;
        m_jumping = true;

        // Subida y bajada con curvas opuestas: SineOut arranca rapido y frena
        // arriba, SineIn hace lo contrario al caer. Da la forma de una parabola sin
        // tener que simular la fisica.
        m_body->runAction(CCSequence::create(
            CCEaseSineOut::create(CCMoveBy::create(kUpTime,   {0.f,  kHeight})),
            CCEaseSineIn::create( CCMoveBy::create(kDownTime, {0.f, -kHeight})),
            CCCallFunc::create(this, callfunc_selector(SheepBouncer::onLanded)),
            nullptr
        ));

        // Media vuelta por brinco, como el cubo de GD. Es independiente del resto:
        // quitar esta linea solo elimina el giro.
        m_body->runAction(CCRotateBy::create(kUpTime + kDownTime, 180.f));

        // La sombra encoge y se aclara con la misma curva que la altura, y en eso
        // consiste el efecto: con una curva propia se separaria del salto y se
        // notaria enseguida que no van juntas.
        if (m_shadow) {
            m_shadow->stopAllActions();
            m_shadow->runAction(CCSequence::create(
                CCEaseSineOut::create(CCScaleTo::create(kUpTime,   m_shadowBase * kShadowMin)),
                CCEaseSineIn::create( CCScaleTo::create(kDownTime, m_shadowBase)),
                nullptr
            ));
            m_shadow->runAction(CCSequence::create(
                CCEaseSineOut::create(CCFadeTo::create(kUpTime,   kShadowAlphaHigh)),
                CCEaseSineIn::create( CCFadeTo::create(kDownTime, kShadowAlphaGround)),
                nullptr
            ));
        }
    }

    // Vuelve a leer los colores del jugador. La escena la llama en su tick mientras
    // las ovejas estan visibles, asi que siempre llevan los colores actuales.
    void refreshColors() {
        auto gm = GameManager::sharedState();
        if (!gm) return;

        // m_playerColor y m_playerColor2 son SeedValueRSV (valores ofuscados) y se
        // leen con .value(). Los getters getPlayerColor y getPlayerColor2 estan
        // marcados como "Out of line" en los bindings y no se pueden llamar; los
        // campos publicos si se pueden leer.
        auto c1 = gm->colorForIdx(gm->m_playerColor.value());
        auto c2 = gm->colorForIdx(gm->m_playerColor2.value());

        bool glow = gm->m_playerGlow;
        auto cg   = glow ? gm->colorForIdx(gm->m_playerGlowColor.value())
                         : ccColor3B{0, 0, 0};

        if (m_invert) std::swap(c1, c2);
        sheep::icon::apply(m_layers, true, glow, c1, c2, cg);
    }

protected:
    static constexpr float kHeight   = 68.f;   // altura del brinco
    static constexpr float kUpTime   = 0.33f;  // duracion de la subida, en segundos
    static constexpr float kDownTime = 0.30f;  // baja un poco mas rapido de lo que sube

    static constexpr float kShadowWidth = 35.f;   // ancho de la sombra en el suelo
    static constexpr float kShadowDrop  = 14.f;   // cuanto por debajo de la oveja
    static constexpr float kShadowMin   = 0.25f;  // fraccion de su tamano en el punto mas alto

    // Opacidad de la sombra en el suelo y en el punto mas alto del brinco.
    static constexpr GLubyte kShadowAlphaGround = 130;
    static constexpr GLubyte kShadowAlphaHigh   = 70;

    // Monta la sombra y el cuerpo con las capas de Sheepy. Devuelve false si falta
    // el arte del cuerpo; sin sombra, en cambio, el Sheepy funciona igual.
    bool init(bool invert) {
        if (!CCNode::init()) return false;
        m_invert = invert;

        // La sombra no cuelga del cuerpo: se queda en el suelo mientras el cuerpo
        // sube. Por eso son dos hijos hermanos y no uno dentro del otro.
        if (auto sh = sheep::createOwn("Sheepy_Player_shadow.png"_spr)) {
            // Se escala por ancho y no con un factor fijo, para que siga midiendo
            // lo mismo si algun dia se reexporta el PNG a otra resolucion.
            m_shadowBase = kShadowWidth / std::max(1.f, sh->getContentSize().width);
            sh->setScale(m_shadowBase);
            sh->setOpacity(kShadowAlphaGround);
            sh->setPosition({0.f, -kShadowDrop});
            this->addChild(sh, 0);
            m_shadow = sh;
        } else {
            // Se reutiliza la llave generica de "falta un recurso propio" en vez de
            // crear una para este archivo: el mensaje es el mismo, y una llave menos
            // es una traduccion menos que mantener en los tres idiomas.
            log::warn("[SheepGD] {}", sheep::trf("Log_MissingOwnAsset", "Sheepy_Player_shadow.png"));
        }

        m_body = CCNode::create();
        this->addChild(m_body, 1);

        if (!sheep::icon::build(m_body, m_layers)) return false;
        refreshColors();
        return true;
    }

    // Final de la secuencia del brinco: permite el siguiente.
    void onLanded() { m_jumping = false; }

private:
    sheep::icon::Layers m_layers;              // las tres capas de Sheepy, dentro de m_body
    cocos2d::CCNode*    m_body   = nullptr;   // lo que salta
    cocos2d::CCSprite*  m_shadow = nullptr;   // lo que se queda en el suelo
    float m_shadowBase = 1.f;                 // escala de la sombra en el suelo
    bool  m_invert     = false;               // colores 1 y 2 intercambiados
    bool  m_jumping    = false;   // un brinco cada vez: pulsar la tecla repetidamente no lo acelera
};