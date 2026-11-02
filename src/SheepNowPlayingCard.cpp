#include "SheepNowPlayingCard.hpp"
#include "SheepAssets.hpp"
#include "SheepThumbs.hpp"
#include "SheepDraw.hpp"
#include "SheepTime.hpp"
#include "SheepI18n.hpp"

using namespace cocos2d;
using namespace cocos2d::extension;

// --- Constantes de maquetacion: para mover elementos, cambiar estos valores -----
namespace {
    constexpr float kPadding      = 7.f;    // marco blanco: suma de los dos lados
    constexpr float kFaceX        = 38.f;   // centro de la cara de dificultad
    constexpr float kInfoX        = 74.f;   // donde empieza la columna de texto
    constexpr float kInfoMaxWidth = 190.f;  // ancho maximo de los textos de esa columna

    // El panel oscuro de la izquierda tiene el borde derecho en diagonal: estas son
    // las X de ese borde abajo y arriba, como fraccion del ancho.
    constexpr float kDarkBottomRatio = 0.60f; // borde diagonal: X abajo
    constexpr float kDarkTopRatio    = 0.68f; // borde diagonal: X arriba
    constexpr float kStatIconHeight   = 15.f;   // alto de los iconos de estadisticas
    constexpr float kStatLabelScale   = 0.42f;  // escala del texto
    constexpr float kStatMinScale     = 0.80f;  // la fila nunca se encoge mas que esto
    constexpr float kStatsRightMargin = 85.f;   // margen hasta el borde derecho
    constexpr float kStatGap          = 5.f;    // icono -> su texto
    constexpr float kStatBlockGap     = 14.f;   // entre bloques
    constexpr float kCoinOverlap = -6.f;   // negativo = las monedas se solapan
    constexpr float kCornerRadius   = 10.f;   // radio de las esquinas

    // Posiciones verticales de la columna de informacion, medidas desde arriba de
    // la tarjeta. Para reordenar o juntar las lineas, basta con cambiar estos
    // numeros.
    constexpr float kRowTitle  = 20.f;
    constexpr float kRowAuthor = 40.f;
    constexpr float kRowBadges = 61.f;   // centro de la columna GDDL/AREDL
    constexpr float kRowStats  = 86.f;
    constexpr float kRowSentBy = 104.f;

    constexpr float kBadgeIconHeight = 14.f;
    constexpr float kBadgeLabelScale = 0.40f;
    constexpr float kBadgeGap        = 16.f;   // entre GDDL y AREDL

    constexpr float kTimerScale       = 0.45f;
    constexpr float kTimerRightMargin = 12.f;   // desde el borde diagonal
    constexpr int   kTimerWarnSeconds = 30;     // por debajo, en rojo

    // Orbes: cuantas lleva ganadas el jugador de las que da el nivel.
    //
    // Lo ganado no puede salir del JSON: 'Orbs' viene de los servidores de GD y
    // dice lo que el nivel vale, no lo que el jugador ha ganado. Eso lo sabe el
    // juego, y para preguntarselo hace falta un GJGameLevel; de ahi
    // getSavedLevel(id), que devuelve la copia local con el progreso. Si el nivel
    // no se ha jugado nunca no hay copia, y entonces lo ganado es 0 por definicion
    // y el total lo pone el JSON.
    struct OrbProgress { int awarded = 0; int total = 0; };

    OrbProgress orbProgress(int levelId, int jsonOrbs) {
        OrbProgress out{0, jsonOrbs};
        if (levelId <= 0) return out;

        auto glm = GameLevelManager::sharedState();
        auto gsm = GameStatsManager::sharedState();
        if (!glm || !gsm) return out;

        // Nota: la sobrecarga getSavedLevel(GJGameLevel*) esta marcada como
        // "Windows: Out of line" en los bindings y no se puede llamar. La que
        // recibe un int si.
        auto saved = glm->getSavedLevel(levelId);
        if (!saved) return out;   // nunca jugado: 0 ganadas, total del JSON

        out.awarded = gsm->getAwardedCurrencyForLevel(saved);

        // El total se queda con el del JSON, a proposito. Una version anterior
        // sacaba los dos numeros de GD, y esta lo revierte.
        //
        // getBaseCurrencyForLevel no es el maximo del nivel: en uno de 10 estrellas
        // devuelve 400 cuando el maximo real (y lo que el jugador acaba teniendo)
        // son 500. De ahi salia "500/400" al completarlo y "0/400" sin completar.
        // El del JSON si es el correcto: SheepGDRequest lo saca de la tabla de
        // estrellas de GD (10 -> 500).
        //
        // La version anterior se escribio para que no pudiera aparecer un valor
        // absurdo, y resulto ser justo lo que lo producia.

        // Red de seguridad, no un parche: si algun dia las dos fuentes no cuadran,
        // en pantalla nunca sale un valor absurdo, pero en el log queda registrado.
        if (out.total > 0 && out.awarded > out.total) {
            log::warn("[SheepGD] {}", sheep::trf("Log_OrbsClamped",
                      levelId, out.awarded, out.total));
            out.awarded = out.total;
        }

        return out;
    }
}

SheepNowPlayingCard* SheepNowPlayingCard::create(CCSize const& size) {
    auto ret = new SheepNowPlayingCard();
    if (ret && ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// Monta todos los elementos de la tarjeta, de atras hacia delante. La tarjeta
// nace en estado vacio ("esperando peticiones") hasta el primer setLevel.
bool SheepNowPlayingCard::init(CCSize const& size) {
    if (!CCNode::init()) return false;

    m_size = size;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});

    buildBackground();
    buildLeftColumn();
    buildInfoColumn();
    buildButtons();

    setEmptyState(true);
    // Medio segundo. El reloj cuenta en segundos enteros, asi que sondear al doble
    // de velocidad basta para que ningun cambio se vea con retraso.
    this->schedule(schedule_selector(SheepNowPlayingCard::updateTimer), 0.5f);
    return true;
}

// ---------------------------------------------------------------------------
// Construccion
// ---------------------------------------------------------------------------

// Fondo de la tarjeta, por capas (z de menor a mayor): relleno oscuro, miniatura
// del nivel, panel oscuro diagonal con su filo claro, y el marco blanco encima.
void SheepNowPlayingCard::buildBackground() {
    const float w     = m_size.width;
    const float h     = m_size.height;
    const float inset = kPadding / 2.f;

    // Los dos contornos comparten centro de curvatura, asi que sus puntos quedan
    // alineados radialmente. Eso es lo que permite dibujar el aro por tramos.
    auto outerPts = sheep::roundedRectPoints({w, h}, kCornerRadius);
    auto innerPts = sheep::roundedRectPoints({w - kPadding, h - kPadding},
                                      kCornerRadius - inset, {inset, inset});

    // --- Atras: relleno oscuro a tamano completo -------------------------
    // Va con la forma exterior, no la interior: con la interior podria verse una
    // costura de un pixel donde empieza el aro.
    auto fill = CCDrawNode::create();
    fill->drawPolygon(outerPts.data(), (unsigned int)outerPts.size(),
                      ccc4f(38 / 255.f, 38 / 255.f, 46 / 255.f, 1.f), 0.f, ccc4f(0, 0, 0, 0));
    this->addChild(fill, 0);

    // --- Medio: miniatura ------------------------------------------------
    // Un contenedor vacio; refreshThumbnail le cuelga el sprite cuando llega.
    m_thumbHolder = CCNode::create();
    m_thumbHolder->setContentSize(m_size);
    this->addChild(m_thumbHolder, 1);

    // Panel oscuro diagonal encima de la miniatura: deja ver la imagen solo en la
    // parte derecha y da contraste a los textos de la izquierda.
    const float pw = w - kPadding;
    const float ph = h - kPadding;

    auto poly = CCDrawNode::create();
    CCPoint verts[4] = {
        {inset,                         inset},
        {inset + pw * kDarkBottomRatio, inset},
        {inset + pw * kDarkTopRatio,    inset + ph},
        {inset,                         inset + ph}
    };
    poly->drawPolygon(verts, 4, ccc4f(38 / 255.f, 38 / 255.f, 46 / 255.f, 0.94f), 0.f, ccc4f(0, 0, 0, 0));
    this->addChild(poly, 2);

    // Filo claro de 3 puntos sobre el borde diagonal.
    auto edge = CCDrawNode::create();
    CCPoint edgeVerts[4] = {
        {inset + pw * kDarkBottomRatio,       inset},
        {inset + pw * kDarkBottomRatio + 3.f, inset},
        {inset + pw * kDarkTopRatio + 3.f,    inset + ph},
        {inset + pw * kDarkTopRatio,          inset + ph}
    };
    edge->drawPolygon(edgeVerts, 4, ccc4f(1.f, 1.f, 1.f, 0.22f), 0.f, ccc4f(0, 0, 0, 0));
    this->addChild(edge, 3);

    // --- Delante: aro blanco que tapa las esquinas cuadradas -------------
    // La miniatura y el panel son rectangulos; el aro redondeado los recorta a la
    // vista sin necesidad de un CCClippingNode.
    auto border = CCDrawNode::create();
    sheep::drawRing(border, outerPts, innerPts, ccc4f(1.f, 1.f, 1.f, 1.f));
    this->addChild(border, 4);
}

// Columna izquierda: la cara de dificultad y, debajo, las estrellas (o lunas).
// El contenido lo pone refreshFace.
void SheepNowPlayingCard::buildLeftColumn() {
    m_faceHolder = CCNode::create();
    m_faceHolder->setPosition({kFaceX, m_size.height - 44.f});
    this->addChild(m_faceHolder, 10);

    m_starsLabel = CCLabelBMFont::create("0", "bigFont.fnt");
    m_starsLabel->setAnchorPoint({1.f, 0.5f});
    m_starsLabel->setScale(0.42f);
    m_starsLabel->setPosition({kFaceX + 2.f, m_size.height - 88.f});
    this->addChild(m_starsLabel, 10);

    if (auto icon = sheep::iconStars(false)) {
        m_starsIcon = icon;
        m_starsIcon->setAnchorPoint({0.f, 0.5f});
        m_starsIcon->setScale(0.42f);
        m_starsIcon->setPosition({kFaceX + 7.f, m_size.height - 88.f});
        this->addChild(m_starsIcon, 10);
    }
}

// Columna de informacion, de arriba abajo: titulo, autor, insignias GDDL/AREDL,
// estadisticas y "Sent by". Ademas, el texto de estado vacio y el reloj del
// auto-skip.
void SheepNowPlayingCard::buildInfoColumn() {
    float h = m_size.height;

    m_titleLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setPosition({kInfoX, h - kRowTitle});
    this->addChild(m_titleLabel, 10);

    m_authorLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_authorLabel->setAnchorPoint({0.f, 0.5f});
    m_authorLabel->setColor({200, 190, 255});
    m_authorLabel->setPosition({kInfoX, h - kRowAuthor});
    this->addChild(m_authorLabel, 10);

    m_ratingRow = CCNode::create();
    m_ratingRow->setAnchorPoint({0.f, 0.5f});
    m_ratingRow->setPosition({kInfoX, h - kRowBadges});
    this->addChild(m_ratingRow, 10);

    m_statsRow = CCNode::create();
    m_statsRow->setAnchorPoint({0.f, 0.5f});
    m_statsRow->setPosition({kInfoX, h - kRowStats});
    this->addChild(m_statsRow, 10);

    m_platformHolder = CCNode::create();
    m_platformHolder->setPosition({kInfoX + 6.f, h - kRowSentBy});
    this->addChild(m_platformHolder, 10);

    m_sentByLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_sentByLabel->setAnchorPoint({0.f, 0.5f});
    m_sentByLabel->setScale(0.4f);
    m_sentByLabel->setPosition({kInfoX + 16.f, h - kRowSentBy});
    this->addChild(m_sentByLabel, 10);

    // Texto del estado vacio. Se crea una sola vez, asi que si cambia el idioma no
    // se actualiza hasta volver a entrar en la escena.
    m_emptyLabel = CCLabelBMFont::create(sheep::tr("Card_Waiting").c_str(), "bigFont.fnt");
    m_emptyLabel->setScale(0.55f);
    m_emptyLabel->setPosition({m_size.width / 2, m_size.height / 2});
    this->addChild(m_emptyLabel, 20);

    // Reloj del auto-skip. Va junto al borde diagonal, a la altura del "Sent by":
    // es el unico hueco ancho que queda libre en la tarjeta.
    m_timerLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_timerLabel->setAnchorPoint({1.f, 0.5f});
    m_timerLabel->setScale(kTimerScale);
    m_timerLabel->setPosition({
        m_size.width * kDarkBottomRatio - kTimerRightMargin,
        m_size.height - kRowSentBy
    });
    m_timerLabel->setVisible(false);
    this->addChild(m_timerLabel, 12);
}

// Botones de la derecha: Play (grande), View, papelera y dado. Cada uno llama a
// su handle*, que a su vez llama al callback que haya puesto la escena.
void SheepNowPlayingCard::buildButtons() {
    m_buttonMenu = CCMenu::create();
    m_buttonMenu->setPosition({0, 0});
    this->addChild(m_buttonMenu, 15);

    float w = m_size.width;
    float h = m_size.height;

    // Los PNG ya son el boton entero, asi que van directos al CCMenuItemSpriteExtra,
    // sin CircleButtonSprite: ese dibujaba un circulo verde y metia el icono
    // dentro, y era el circulo de mas que se veia.
    constexpr float kBigBtn   = 42.f;   // alto del boton de play
    constexpr float kSmallBtn = 30.f;   // alto de la papelera y del dado
    constexpr float kViewH    = 26.f;   // alto del boton View

    if (auto spr = sheep::ownSprite("play_btn.png"_spr, kBigBtn)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepNowPlayingCard::handlePlay));
        btn->setPosition({w - 42.f, h - 44.f});
        m_buttonMenu->addChild(btn);
    }

    // El texto "View" se pone una sola vez, como el del estado vacio.
    if (auto spr = sheep::ownTextButton(sheep::tr("Btn_View").c_str(), kViewH)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepNowPlayingCard::handleView));
        btn->setPosition({w - 42.f, 26.f});
        m_buttonMenu->addChild(btn);
    }

    if (auto spr = sheep::ownSprite("trash.png"_spr, kSmallBtn)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepNowPlayingCard::handleDelete));
        btn->setPosition({w - 88.f, h - 44.f});
        m_buttonMenu->addChild(btn);
        m_deleteBtn = btn;
    }

    if (auto spr = sheep::ownSprite("dice.png"_spr, kSmallBtn)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepNowPlayingCard::handleRandom));
        btn->setPosition({w - 128.f, h - 44.f});
        m_buttonMenu->addChild(btn);
        m_randomBtn = btn;
    }
}

// ---------------------------------------------------------------------------
// Datos
// ---------------------------------------------------------------------------

// Muestra un nivel. Si no es valido (el centinela WAITING), la tarjeta pasa a
// estado vacio y no se rellena nada mas.
void SheepNowPlayingCard::setLevel(LevelModel const& level) {
    m_level = level;
    m_hasLevel = level.isValid();

    setEmptyState(!m_hasLevel);
    if (!m_hasLevel) return;

    refreshFace();
    refreshTexts();
    refreshRatingRow();
    refreshStatsRow();
    refreshThumbnail();
    updateTimer(0.f);   // sin esperar al primer tick del schedule
}

// Modo placeholder: el nivel que se muestra no es una peticion. Hay que llamarlo
// antes de setLevel, porque refreshTexts lo consulta.
//
// La papelera y el dado se ocultan, y no es cosmetico: borrar o mandar al
// historial un nivel que no es una peticion no tiene sentido. Play y View si lo
// tienen, asi que se quedan.
//
// Se hace boton por boton y no con m_buttonMenu, que es el que usa setEmptyState
// para ocultarlo todo a la vez: son dos ejes distintos y no deben pisarse.
void SheepNowPlayingCard::setPlaceholder(bool on) {
    m_placeholder = on;
    if (m_deleteBtn) m_deleteBtn->setVisible(!on);
    if (m_randomBtn) m_randomBtn->setVisible(!on);
}

// Muestra el texto de "esperando peticiones" y oculta todo lo demas, o al reves.
void SheepNowPlayingCard::setEmptyState(bool empty) {
    m_emptyLabel->setVisible(empty);

    for (auto node : {(CCNode*)m_titleLabel, (CCNode*)m_authorLabel, (CCNode*)m_ratingRow,
                      (CCNode*)m_sentByLabel, (CCNode*)m_statsRow, (CCNode*)m_faceHolder,
                      (CCNode*)m_starsLabel, (CCNode*)m_platformHolder}) {
        if (node) node->setVisible(!empty);
    }
    if (m_starsIcon)   m_starsIcon->setVisible(!empty);
    if (m_buttonMenu)  m_buttonMenu->setVisible(!empty);
    if (m_thumbSprite) m_thumbSprite->setVisible(!empty);
}

// Actualiza el reloj del auto-skip. Lo llama el schedule cada 0,5 s y setLevel al
// cambiar de nivel.
void SheepNowPlayingCard::updateTimer(float) {
    if (!m_timerLabel) return;

    // Se oculta en vez de mostrar "0:00": un cero fijo en pantalla se lee como un
    // fallo, y la ausencia de reloj se lee como "este nivel no tiene auto-skip".
    if (!m_hasLevel || m_level.autoSkipEndsAt <= 0) {
        m_timerLabel->setVisible(false);
        return;
    }

    auto left = sheep::secondsLeft(m_level.autoSkipEndsAt);
    if (left <= 0) {
        m_timerLabel->setVisible(false);
        return;
    }

    m_timerLabel->setVisible(true);
    m_timerLabel->setString(sheep::formatClock(left).c_str());
    m_timerLabel->setColor(left <= kTimerWarnSeconds ? ccColor3B{255, 90, 90}
                                                     : ccColor3B{255, 255, 255});
}

// Cara de dificultad con su halo, y el numero de estrellas (o lunas) debajo.
void SheepNowPlayingCard::refreshFace() {
    m_faceHolder->removeAllChildren();

    int idx = sheep::difficultyIndex(m_level.difficultyName, m_level.stars);

    // GJDifficultySprite es la clase del juego: elige el frame correcto y, con
    // updateFeatureState, dibuja el halo. Se usa la version larga, la que lleva
    // la palabra ("DEMON") debajo de la cara.
    if (auto face = sheep::difficultyFaceNative(idx)) {
        face->updateFeatureState(sheep::featureState(m_level.featureGlowPath, m_level.hasFeatureGlow));
        face->setPosition({0, 0});
        m_faceHolder->addChild(face);
    }

    bool plat = sheep::isPlatformer(m_level.length);
    m_starsLabel->setString(std::to_string(m_level.stars).c_str());

    // El icono se rehace en cada nivel porque cambia entre estrella y luna.
    if (m_starsIcon) {
        m_starsIcon->removeFromParent();
        m_starsIcon = nullptr;
    }
    if (auto icon = sheep::iconStars(plat)) {
        m_starsIcon = icon;
        m_starsIcon->setAnchorPoint({0.f, 0.5f});
        m_starsIcon->setScale(0.42f);
        m_starsIcon->setPosition({kFaceX + 7.f, m_size.height - 88.f});
        this->addChild(m_starsIcon, 10);
    }
}

// Titulo, autor, "Sent by" e icono de la plataforma. Los textos largos se encogen
// con limitLabelWidth hasta una escala minima.
void SheepNowPlayingCard::refreshTexts() {
    m_titleLabel->setString(m_level.name.c_str());
    m_titleLabel->limitLabelWidth(kInfoMaxWidth, 0.72f, 0.30f);

    m_authorLabel->setString(sheep::trf("Card_By", m_level.author).c_str());
    m_authorLabel->limitLabelWidth(kInfoMaxWidth, 0.46f, 0.24f);

    // El renglon del "Sent by" es donde se indica que el nivel no es una peticion.
    std::string sent = m_placeholder ? sheep::tr("Card_Placeholder")
                                     : sheep::trf("Card_SentBy", m_level.requesterName);
    m_sentByLabel->setString(sent.c_str());
    m_sentByLabel->limitLabelWidth(kInfoMaxWidth - 16.f, 0.40f, 0.22f);

    m_platformHolder->removeAllChildren();
    if (auto icon = sheep::platformIcon(m_level.platformIconPath)) {
        icon->setScale(11.f / std::max(1.f, icon->getContentSize().height));
        m_platformHolder->addChild(icon);
    }
}

// Insignias de GDDL y AREDL, cada una como una columna con el icono arriba y el
// valor debajo. Solo aparecen las de las listas en las que esta el nivel (campo no
// vacio en el JSON).
void SheepNowPlayingCard::refreshRatingRow() {
    m_ratingRow->removeAllChildren();

    struct Badge { CCSprite* icon; std::string text; ccColor3B color; };
    std::vector<Badge> badges;

    // GDDL: el tier, con el color que manda SheepGDRequest (rosa si no se puede leer).
    if (!m_level.gddlTier.empty())
        badges.push_back({sheep::iconGddl(), m_level.gddlTier,
                          sheep::parseHexColor(m_level.gddlTierColor, {255, 120, 220})});

    if (m_level.hasAredl && !m_level.aredlRank.empty()) {
        // El '#' lo escribe C# dentro del propio campo ($"#{pos}"), asi que anadir
        // otro daba "##1341"; y en bigFont el '#' se dibuja como "No", de ahi el
        // "NoNo1341" que se veia en la tarjeta.
        //
        // Se normaliza en vez de limitarse a no anadir el '#': hay dos programas
        // escribiendo esta cadena, y asi da igual cual de los dos cambie de formato.
        std::string rank = m_level.aredlRank;
        while (!rank.empty() && rank.front() == '#') rank.erase(rank.begin());
        badges.push_back({sheep::iconAredl(), "#" + rank, {255, 255, 255}});
    }

    float x = 0.f;
    for (auto const& b : badges) {
        // Cada insignia es una columna con su origen en el centro vertical: el
        // icono se apoya hacia arriba y el numero cuelga hacia abajo.
        auto column = CCNode::create();
        float colWidth = 0.f;

        if (b.icon) {
            float s = kBadgeIconHeight / std::max(1.f, b.icon->getContentSize().height);
            b.icon->setScale(s);
            b.icon->setAnchorPoint({0.5f, 0.f});   // borde inferior en el centro
            b.icon->setPosition({0.f, 1.f});
            column->addChild(b.icon);
            colWidth = std::max(colWidth, b.icon->getContentSize().width * s);
        }

        auto label = CCLabelBMFont::create(b.text.c_str(), "bigFont.fnt");
        label->setScale(kBadgeLabelScale);
        label->setAnchorPoint({0.5f, 1.f});        // borde superior en el centro
        label->setColor(b.color);
        label->setPosition({0.f, -1.f});
        column->addChild(label);
        colWidth = std::max(colWidth, label->getContentSize().width * kBadgeLabelScale);

        // La columna se centra sobre su propio ancho.
        column->setPosition({x + colWidth / 2.f, 0.f});
        m_ratingRow->addChild(column);

        x += colWidth + kBadgeGap;
    }

    // Si no cabe, se encoge la fila entera.
    float maxWidth = m_size.width - kInfoX - kStatsRightMargin;
    m_ratingRow->setScale((x > maxWidth && x > 0.f) ? maxWidth / x : 1.f);
}

// Fila de estadisticas: monedas, duracion, descargas, likes y orbes.
void SheepNowPlayingCard::refreshStatsRow() {
    m_statsRow->removeAllChildren();

    std::vector<sheep::RowItem> stats;

    // Monedas: una por moneda, hasta tres. El JSON solo trae cuantas hay, no si
    // estan verificadas, asi que siempre se usa el icono de moneda sin verificar.
    if (m_level.coins > 0) {
        int total = std::min(m_level.coins, 3);

        for (int i = 0; i < total; ++i) {
            if (auto c = sheep::iconCoin(false)) {
                // El solapamiento va entre monedas. La ultima no lo lleva, para que
                // use la separacion normal y no se junte con el icono de duracion.
                std::optional<float> gap;
                if (i < total - 1) gap = kCoinOverlap;

                stats.push_back({c, "", {255, 255, 255}, gap});
            }
        }
    }
    if (!m_level.length.empty())    stats.push_back({sheep::iconLength(),    m_level.length});
    if (!m_level.downloads.empty()) stats.push_back({sheep::iconDownloads(), m_level.downloads});
    if (!m_level.likes.empty())     stats.push_back({sheep::iconLikes(m_level.isDislike), m_level.likes});
    if (m_level.orbs > 0) {
        auto orbs = orbProgress(m_level.levelIdAsInt(), m_level.orbs);
        stats.push_back({sheep::iconOrbs(), fmt::format("{}/{}", orbs.awarded, orbs.total)});
    }
    float width = sheep::layoutIconTextRow(m_statsRow, stats,
                                    kStatIconHeight, kStatLabelScale, kStatGap, kStatBlockGap);

    // El limite es el ancho de la tarjeta menos la columna de la izquierda y el
    // margen derecho: la fila puede entrar en la zona de la miniatura, que es lo
    // que se buscaba. Por debajo de kStatMinScale no se encoge mas.
    float maxWidth = m_size.width - kInfoX - kStatsRightMargin;
    float scale = (width > maxWidth && width > 0.f) ? maxWidth / width : 1.f;
    m_statsRow->setScale(std::max(scale, kStatMinScale));
}

// Miniatura del nivel. Si esta en cache (memoria o disco) se dibuja ya; si no, se
// pide a la red y esta funcion se vuelve a llamar cuando llega.
void SheepNowPlayingCard::refreshThumbnail() {
    if (m_thumbSprite) {
        m_thumbSprite->removeFromParent();
        m_thumbSprite = nullptr;
    }
    if (m_level.levelId.empty()) return;

    auto tex = sheep::cachedThumb(m_level.levelId);
    if (!tex) {
        // No esta en cache: se pide y se sale. Cuando llega, requestThumb la guarda
        // en cache y el callback vuelve a llamar a refreshThumbnail().
        //
        // m_thumbRequestedFor evita apuntarse dos veces a la misma miniatura. Una
        // descarga fallida no se reintenta mientras la tarjeta siga en ese nivel
        // (y requestThumb, ademas, no la reintenta en un rato).
        if (m_thumbRequestedFor != m_level.levelId) {
            m_thumbRequestedFor = m_level.levelId;
            auto url = sheep::thumbUrl(m_level.levelId, m_level.thumbnailUrl);

            // Ref(this) es una referencia fuerte a la tarjeta. Sin ella, salir de la
            // escena a mitad de descarga haria que el callback usara memoria ya
            // liberada. levelId se copia por valor porque, cuando llegue la
            // respuesta, m_level puede ser otro nivel.
            //
            // Si el nivel cambio durante la descarga, la miniatura se guarda en cache
            // igual (sirve para despues) pero no se redibuja. Se mira tambien
            // m_hasLevel: el centinela WAITING conserva el LevelId del nivel
            // anterior, y sin eso la miniatura se dibujaria sobre la tarjeta vacia.
            sheep::requestThumb(m_level.levelId, url,
                [self = Ref(this), levelId = m_thumbRequestedFor](CCTexture2D*) {
                    if (self->m_hasLevel && self->m_level.levelId == levelId)
                        self->refreshThumbnail();
                });
        }
        return;
    }

    // Recorte tipo "cover" (llenar la caja sin deformar, cortando lo que sobre)
    // hecho en espacio de textura, para no necesitar un CCClippingNode.
    float texW = tex->getContentSize().width;
    float texH = tex->getContentSize().height;
    if (texW <= 0 || texH <= 0) return;

    float boxW = m_size.width  - kPadding;
    float boxH = m_size.height - kPadding;
    float targetAspect = boxW / boxH;
    float srcAspect    = texW / texH;

    float cropW, cropH;
    if (srcAspect > targetAspect) { cropH = texH; cropW = texH * targetAspect; }
    else                          { cropW = texW; cropH = texW / targetAspect; }

    CCRect rect((texW - cropW) / 2.f, (texH - cropH) / 2.f, cropW, cropH);

    m_thumbSprite = CCSprite::createWithTexture(tex, rect);
    if (!m_thumbSprite) return;

    m_thumbSprite->setScale(boxW / cropW);
    m_thumbSprite->setPosition({m_size.width / 2, m_size.height / 2});
    m_thumbHolder->addChild(m_thumbSprite);
}

// ---------------------------------------------------------------------------
// Botones. Cada uno avisa a la escena por su callback, si lo hay. Play, View y la
// papelera solo actuan con un nivel valido; el dado no lo necesita, porque elige
// de la cola.
// ---------------------------------------------------------------------------

void SheepNowPlayingCard::handlePlay(CCObject*)   { if (onPlay   && m_hasLevel) onPlay(m_level); }
void SheepNowPlayingCard::handleView(CCObject*)   { if (onView   && m_hasLevel) onView(m_level); }
void SheepNowPlayingCard::handleDelete(CCObject*) { if (onDelete && m_hasLevel) onDelete(m_level); }
void SheepNowPlayingCard::handleRandom(CCObject*) { if (onRandom) onRandom(); }