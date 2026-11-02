#include "SheepSettingsPopup.hpp"
#include "SheepAssets.hpp"
#include "SheepData.hpp"
#include "SheepI18n.hpp"

using namespace cocos2d;

// Constantes de maquetacion y utilidades internas de este archivo. Las posiciones
// son en puntos dentro del popup, con el origen abajo a la izquierda.
namespace {
    constexpr float kW = 360.f;   // ancho del popup
    constexpr float kH = 262.f;   // alto del popup (antes 320, la pantalla entera)

    constexpr float kColL = 92.f;    // centro de la columna izquierda
    constexpr float kColR = 268.f;   // y de la derecha

    // El ritmo vertical sale de un solo numero: la fila 2 se deriva de la 1. Antes
    // eran dos alturas sueltas y cuadrarlas obligaba a cambiar las dos.
    constexpr float kRowGap = 44.f;
    constexpr float kRow1   = 212.f;
    constexpr float kRow2   = kRow1 - kRowGap;

    constexpr float kEndLabelY = 114.f;   // "WHEN THE QUEUE ENDS"
    constexpr float kEndValueY = 90.f;    // el selector con sus flechas
    constexpr float kEndArrowX = 74.f;    // separacion de las flechas al centro
    constexpr float kInputY    = 52.f;    // campo del nivel placeholder

    // "Swap on death" va en horizontal (etiqueta y casilla en la misma linea) para
    // no gastar una fila entera en un solo interruptor.
    constexpr float kSwapY      = 20.f;
    constexpr float kSwapLabelX = 160.f;
    constexpr float kSwapCheckX = 230.f;

    constexpr float kCheckDrop  = 20.f;   // texto -> su casilla
    constexpr float kLabelScale = 0.34f;
    constexpr float kCheckScale = 0.60f;
    constexpr float kMaxLabelW  = 140.f;  // ancho maximo de las etiquetas
    constexpr float kArrowH     = 26.f;   // alto de las flechas del selector

    // El lienzo de sheep_panel.png y el grosor de su borde decorado, en pixeles
    // del PNG. No se usan tal cual: ver el comentario de setup().
    constexpr float kPanelSrcPx   = 320.f;
    constexpr float kPanelInsetPx = 40.f;
    constexpr float kCloseHeight = 30.f;   // alto del boton de cerrar, en puntos

    // Casilla hecha a mano en vez de CCMenuItemToggler. El "activado" de GD no es
    // una marca suelta sino la caja entera con la marca dentro, asi que las dos
    // imagenes no se superponen: una tapa a la otra. Se dejan las dos y se juega
    // con la visibilidad de la marca, lo que ademas evita depender del orden de
    // argumentos del toggler.
    //
    // Devuelve el boton (la caja vacia) y deja en outOn el sprite de "activado",
    // que es el que hay que mostrar u ocultar.
    CCMenuItemSpriteExtra* makeCheckbox(CCMenu* menu, CCPoint const& pos,
                                        CCObject* target, SEL_MenuHandler sel,
                                        CCSprite*& outOn) {
        auto off = sheep::frameSprite("GJ_checkOff_001.png");
        if (!off) return nullptr;
        off->setScale(kCheckScale);

        auto item = CCMenuItemSpriteExtra::create(off, target, sel);
        item->setPosition(pos);
        menu->addChild(item);

        if (auto on = sheep::frameSprite("GJ_checkOn_001.png")) {
            on->setScale(kCheckScale);
            on->setPosition({item->getContentSize().width  / 2.f,
                             item->getContentSize().height / 2.f});
            item->addChild(on, 10);
            outOn = on;
        }
        return item;
    }
}

// Crea el popup con el fondo propio del mod, o con el del juego si falta.
SheepSettingsPopup* SheepSettingsPopup::create() {
    auto ret = new SheepSettingsPopup();
    if (!ret) return nullptr;

    // Fondo propio en vez del GJ_square01 de GD: el panel del juego no encaja con
    // la paleta de la escena y, ademas, un texture pack lo puede cambiar.
    //
    // Se comprueba que el archivo exista antes de init y no despues: init monta el
    // popup entero, asi que no se puede reintentar con otro fondo sin duplicar
    // hijos.
    char const* bg = "sheep_panel.png"_spr;
    if (!sheep::ownSpriteExists(bg)) {
        log::warn("[SheepGD] {}", sheep::tr("Log_PanelFallback"));
        bg = "GJ_square01.png";
    }

    if (ret->init(kW, kH, bg) && ret->setup()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// Monta el contenido: los bordes del fondo, el boton de cerrar, el titulo, los
// cuatro interruptores, el selector de fin de cola, el campo del placeholder y
// "Swap on death".
bool SheepSettingsPopup::setup() {
    if (m_bgSprite) {
        if (auto probe = sheep::createOwn("sheep_panel.png"_spr)) {
            float src = probe->getContentSize().width;   // el lienzo, ya en puntos
            float ins = kPanelInsetPx * (src / kPanelSrcPx);

            // El borde (inset) del nine-slice va en el espacio de contentSize, no en
            // pixeles del PNG: GD aplica un factor de escala de contenido, asi que un
            // lienzo de 320 px puede medir 80 puntos. Por eso se calcula como
            // fraccion del lienzo.
            //
            // Y no se cambia setScaleMultiplier: que el borde conserve su grosor sea
            // cual sea el tamano del panel es justo para lo que sirve un nine-slice,
            // no algo que haya que compensar.
            m_bgSprite->setInsets({ins, ins, ins, ins});
            log::debug("[SheepGD] {}", sheep::trf("Log_PanelInsets", src, ins));
        }
    }

    if (auto close = sheep::createOwn("close.png"_spr)) {
        float s = kCloseHeight / std::max(1.f, close->getContentSize().height);
        this->setCloseButtonSpr(close, s);
    }

    this->setTitle(sheep::tr("Set_Title"));

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 10);

    // Crea una etiqueta y la anade al popup. Recibe std::string y no char const*:
    // todas las etiquetas salen ya de tr(), asi que el .c_str() se hace una vez aqui
    // dentro en vez de en cada llamada.
    auto mkLabel = [&](std::string const& text, float x, float y) -> CCLabelBMFont* {
        auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        // limitLabelWidth y no setScale: unas etiquetas son bastante mas largas que
        // otras, y con una escala fija se saldrian de su columna.
        l->limitLabelWidth(kMaxLabelW, kLabelScale, 0.18f);
        l->setPosition({x, y});
        m_mainLayer->addChild(l, 10);
        return l;
    };

    // --- Filas 1 y 2: los cuatro interruptores ---------------------------
    // Izquierda, el reloj de la partida y su modo "Last 10s"; derecha, el cubo de
    // Sheepy y su modo "Only in requests". La fila 2 depende de la 1 (ver refresh).
    mkLabel(sheep::tr("Set_Timer"), kColL, kRow1);
    makeCheckbox(menu, {kColL, kRow1 - kCheckDrop},
                 this, menu_selector(SheepSettingsPopup::onToggleIngameTimer),
                 m_chkTimerOn);

    m_lastTenLabel = mkLabel(sheep::tr("Set_LastTen"), kColL, kRow2);
    m_lastTenItem  = makeCheckbox(menu, {kColL, kRow2 - kCheckDrop},
                                  this, menu_selector(SheepSettingsPopup::onToggleLastTen),
                                  m_chkLastOn);

    mkLabel(sheep::tr("Set_Sheepy"), kColR, kRow1);
    makeCheckbox(menu, {kColR, kRow1 - kCheckDrop},
                 this, menu_selector(SheepSettingsPopup::onToggleSheepyIcon),
                 m_chkSheepyOn);

    m_onlyReqLabel = mkLabel(sheep::tr("Set_OnlyReq"), kColR, kRow2);
    m_onlyReqItem  = makeCheckbox(menu, {kColR, kRow2 - kCheckDrop},
                                  this, menu_selector(SheepSettingsPopup::onToggleSheepyOnlyRequests),
                                  m_chkOnlyReqOn);

    // --- Fin de cola: selector de cuatro opciones -------------------------
    // Flechas y no cuatro casillas: son opciones excluyentes, y cuatro casillas
    // invitarian a marcar dos.
    mkLabel(sheep::tr("Set_EndOfQueue"), kW / 2.f, kEndLabelY);

    m_endLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_endLabel->setPosition({kW / 2.f, kEndValueY});
    m_mainLayer->addChild(m_endLabel, 10);

    if (auto spr = sheep::ownSprite("arrow.png"_spr, kArrowH)) {
        spr->setFlipX(true);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(SheepSettingsPopup::onEndPrev));
        btn->setPosition({kW / 2.f - kEndArrowX, kEndValueY});
        menu->addChild(btn);
    }
    if (auto spr = sheep::ownSprite("arrow.png"_spr, kArrowH)) {
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(SheepSettingsPopup::onEndNext));
        btn->setPosition({kW / 2.f + kEndArrowX, kEndValueY});
        menu->addChild(btn);
    }

    // --- El ID del nivel placeholder, solo visible en ese modo ------------
    // Se guarda al cerrar el popup (onClose), no con cada tecla: con la cola
    // acabada y el modo placeholder activo, la escena (que sigue con su tick detras
    // del popup) buscaria cada ID a medio escribir: 1, 12, 123...
    m_placeholder = TextInput::create(150.f, sheep::tr("Set_LevelId"), "bigFont.fnt");
    m_placeholder->setCommonFilter(CommonFilter::Uint);   // solo digitos
    m_placeholder->setMaxCharCount(12);
    m_placeholder->setPosition({kW / 2.f, kInputY});
    m_placeholder->setString(sheep::placeholderLevelText());
    m_mainLayer->addChild(m_placeholder, 10);

    // --- Momento del cambio: "Swap on death" ------------------------------
    mkLabel(sheep::tr("Set_SwapOnDeath"), kSwapLabelX, kSwapY);
    makeCheckbox(menu, {kSwapCheckX, kSwapY},
                 this, menu_selector(SheepSettingsPopup::onToggleSwapOnDeath),
                 m_chkSwapDeath);

    refresh();
    return true;
}

// Guarda el ID del placeholder y cierra. Pasa por aqui tanto el boton de cerrar
// como la tecla Escape.
void SheepSettingsPopup::onClose(CCObject* sender) {
    if (m_placeholder) {
        Mod::get()->setSavedValue<std::string>(sheep::kKeyPlaceholderId,
                                               m_placeholder->getString());
    }
    Popup::onClose(sender);
}

// Pone cada control en el estado guardado. Se llama al abrir y tras cada cambio.
//
// Lee con las funciones de SheepData.hpp, y no con getSavedValue y un valor por
// defecto propio: asi el valor por defecto vive en un solo sitio, y el popup no
// puede mostrar una cosa mientras la partida hace otra. Lo mismo en los onToggle*.
void SheepSettingsPopup::refresh() {
    bool timerOn = sheep::ingameTimerEnabled();
    bool lastOn  = sheep::lastTenOnly();
    bool sheepOn = sheep::sheepyEnabled();
    bool onlyReq = sheep::sheepyOnlyRequests();

    if (m_chkTimerOn)   m_chkTimerOn->setVisible(timerOn);
    if (m_chkLastOn)    m_chkLastOn->setVisible(lastOn);
    if (m_chkSheepyOn)  m_chkSheepyOn->setVisible(sheepOn);
    if (m_chkOnlyReqOn) m_chkOnlyReqOn->setVisible(onlyReq);

    // "Last 10s" no tiene sentido con el reloj apagado, asi que su bloque entero
    // desaparece en vez de quedarse ahi sin efecto.
    if (m_lastTenLabel) m_lastTenLabel->setVisible(timerOn);
    if (m_lastTenItem)  m_lastTenItem->setVisible(timerOn);

    // Lo mismo con "Only in requests" y el cubo de Sheepy.
    if (m_onlyReqLabel) m_onlyReqLabel->setVisible(sheepOn);
    if (m_onlyReqItem)  m_onlyReqItem->setVisible(sheepOn);

    // Fin de cola: el nombre del modo y, dependiendo de el, el campo del ID.
    int mode = sheep::endOfQueueMode();
    if (m_endLabel) {
        // .c_str(): endOfQueueName devuelve std::string desde que el nombre del
        // modo sale de la tabla de traducciones.
        m_endLabel->setString(sheep::endOfQueueName(mode).c_str());
        m_endLabel->limitLabelWidth(120.f, 0.42f, 0.22f);
    }
    if (m_placeholder) m_placeholder->setVisible(mode == sheep::kEndPlaceholder);

    if (m_chkSwapDeath) m_chkSwapDeath->setVisible(sheep::swapOnDeath());
}

// Mueve el selector de fin de cola 'delta' posiciones, dando la vuelta en los
// extremos, y lo guarda.
void SheepSettingsPopup::stepEndOfQueue(int delta) {
    // Se suma kEndOfQueueCount antes del modulo: en C++ el resto de un negativo es
    // negativo, asi que sin eso la flecha izquierda desde la opcion 0 daria -1.
    int mode = (sheep::endOfQueueMode() + delta + sheep::kEndOfQueueCount)
               % sheep::kEndOfQueueCount;
    Mod::get()->setSavedValue<int>(sheep::kKeyEndOfQueue, mode);
    refresh();
}

void SheepSettingsPopup::onEndPrev(CCObject*) { stepEndOfQueue(-1); }
void SheepSettingsPopup::onEndNext(CCObject*) { stepEndOfQueue(+1); }

// Los interruptores: cada uno invierte su ajuste guardado y repinta.
void SheepSettingsPopup::onToggleSwapOnDeath(CCObject*) {
    bool v = !sheep::swapOnDeath();
    Mod::get()->setSavedValue<bool>(sheep::kKeySwapOnDeath, v);
    refresh();
}

void SheepSettingsPopup::onToggleIngameTimer(CCObject*) {
    bool v = !sheep::ingameTimerEnabled();
    Mod::get()->setSavedValue<bool>(sheep::kKeyIngameTimer, v);
    refresh();
}

void SheepSettingsPopup::onToggleLastTen(CCObject*) {
    bool v = !sheep::lastTenOnly();
    Mod::get()->setSavedValue<bool>(sheep::kKeyLastTenOnly, v);
    refresh();
}

void SheepSettingsPopup::onToggleSheepyIcon(CCObject*) {
    bool v = !sheep::sheepyEnabled();
    Mod::get()->setSavedValue<bool>(sheep::kKeySheepyIcon, v);
    refresh();
}

void SheepSettingsPopup::onToggleSheepyOnlyRequests(CCObject*) {
    bool v = !sheep::sheepyOnlyRequests();
    Mod::get()->setSavedValue<bool>(sheep::kKeySheepyOnlyReq, v);
    refresh();
}