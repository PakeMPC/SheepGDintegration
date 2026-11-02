#include "SheepQueuePanel.hpp"
#include "SheepAssets.hpp"
#include "SheepThumbs.hpp"
#include "SheepDraw.hpp"
#include "SheepTime.hpp"
#include "SheepI18n.hpp"

using namespace cocos2d;

// Constantes de maquetacion y utilidades internas de este archivo.
namespace {
    constexpr float  kCellHeight = 52.f;    // alto de cada fila
    constexpr float  kAnimTime   = 0.22f;   // duracion de las animaciones de las filas
    constexpr size_t kMaxCells   = 100;     // filas como maximo; el resto no se dibuja

    constexpr float kFaceX    = 26.f;    // centro de la cara de dificultad
    constexpr float kInfoX    = 52.f;    // donde empieza la columna de texto
    constexpr float kBtnAreaW = 128.f;   // ancho reservado a los botones

    constexpr float kRowName   = 40.f;   // alturas desde la base de la celda
    constexpr float kRowSentBy = 25.f;
    constexpr float kRowStats  = 11.f;

    // Corte diagonal del panel oscuro de cada fila: X abajo y X arriba, como
    // fraccion del ancho.
    constexpr float kDarkBottom = 0.58f; // corte diagonal
    constexpr float kDarkTop    = 0.64f;

    // --- Marco propio, en lugar de GJListLayer ---
    constexpr float kBarHeight  = 20.f;  // grosor visible de las barras
    constexpr float kBarInset   =  5.f;  // cuanto invaden la lista para tapar el desborde
    // La esquina mide exactamente lo mismo que la barra: es la unica forma de que
    // los bordes exteriores coincidan. Para que sobresalga arriba y abajo a
    // proposito, habria que subir el factor (1.2 = un 20% mas alta).
    constexpr float kCornerHeight = kBarHeight * 1.0f;
    constexpr float kCornerOut  =  3.f;  // cuanto sobresale la esquina por fuera del ancho
    constexpr float kRailWidth  =  3.f;  // grosor de los rieles laterales (line.png)
    constexpr float kTitleScale = 0.42f;

    // Pone en 'holder' la miniatura recortada tipo "cover" (llena 'size' sin
    // deformar, cortando lo que sobre), igual que en la tarjeta. Sustituye lo que
    // hubiera dentro.
    void addThumbSprite(cocos2d::CCNode* holder, cocos2d::CCTexture2D* tex, cocos2d::CCSize const& size) {
        const float texW = tex->getContentSize().width;
        const float texH = tex->getContentSize().height;
        if (texW <= 0.f || texH <= 0.f) return;

        const float ta = size.width / size.height;
        const float sa = texW / texH;
        float cw, ch;
        if (sa > ta) { ch = texH; cw = texH * ta; }
        else         { cw = texW; ch = texW / ta; }

        cocos2d::CCRect rect((texW - cw) / 2.f, (texH - ch) / 2.f, cw, ch);
        auto spr = cocos2d::CCSprite::createWithTexture(tex, rect);
        if (!spr) return;

        spr->setScale(size.width / cw);
        spr->setPosition({size.width / 2.f, size.height / 2.f});
        holder->removeAllChildren();
        holder->addChild(spr);
    }
}

SheepQueuePanel* SheepQueuePanel::create(CCSize const& listSize) {
    auto ret = new SheepQueuePanel();
    if (ret && ret->init(listSize)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// Monta el marco y deja un mensaje de "cargando" hasta el primer setQueue. El
// nodo tiene el ancla en el centro: todo se coloca respecto al centro de la lista.
bool SheepQueuePanel::init(CCSize const& listSize) {
    if (!CCNode::init()) return false;

    m_listSize = listSize;
    this->setAnchorPoint({0.5f, 0.5f});

    buildFrame();
    showMessage(sheep::tr("Queue_Loading"));
    return true;
}

// El marco y el contenedor del scroll se crean una sola vez y no se vuelven a
// tocar; lo que cambia con los datos son solo las celdas de dentro.
//
// Capas, de atras hacia delante: fondo (z 4), scroll con las celdas (z 5), rieles
// laterales (z 9), barras de arriba y abajo (z 10), esquinas (z 11) y titulo (z 12).
void SheepQueuePanel::buildFrame() {
    const float halfW = m_listSize.width  / 2.f;
    const float halfH = m_listSize.height / 2.f;

    // Fondo del area de la lista. Va en z 4, por detras del scroll: rellena el
    // hueco que deja el rebote al arrastrar mas alla del primer nivel.
    auto listBg = CCLayerColor::create({0x21, 0x21, 0x32, 255});
    listBg->setContentSize(m_listSize);
    listBg->setPosition({-halfW, -halfH});
    this->addChild(listBg, 4);

    // El scroll primero y en z bajo: todo el marco se dibuja por delante.
    m_scroll = ScrollLayer::create(m_listSize);
    m_scroll->setPosition({-halfW, -halfH});
    this->addChild(m_scroll, 5);

    // Centro vertical de cada barra. La barra queda a caballo sobre la linea del
    // borde y baja kBarInset hacia dentro: ese solape es lo unico que tapa las
    // celdas que se salen al desplazar, porque el ScrollLayer no recorta.
    const float barY = halfH + kBarHeight / 2.f - kBarInset;

    // Barras de arriba (i = 0) y de abajo (i = 1).
    for (int i = 0; i < 2; ++i) {
        const bool top = (i == 0);

        auto bar = sheep::createOwn("border.png"_spr);   // avisa en el log si falta
        if (!bar) break;

        auto natural = bar->getContentSize();
        // Escala no uniforme a proposito: el grosor lo fija kBarHeight y el ancho
        // se estira al de la lista. Las lineas negras del PNG son horizontales, asi
        // que estirar en X no las deforma.
        bar->setScaleY(kBarHeight / std::max(1.f, natural.height));
        bar->setScaleX(m_listSize.width / std::max(1.f, natural.width));
        bar->setFlipY(!top);   // la de abajo es la misma imagen del reves

        bar->setPosition({0.f, top ? barY : -barY});
        this->addChild(bar, 10);
    }

    // Rieles laterales. Van en z 9, por detras de las barras y las esquinas: asi
    // sus extremos quedan escondidos debajo y no se ve el corte. Se estiran de
    // centro a centro de las dos barras.
    for (int i = 0; i < 2; ++i) {
        const float sx = (i == 0) ? -1.f : 1.f;

        auto rail = sheep::createOwn("line.png"_spr);   // avisa en el log si falta
        if (!rail) break;

        auto natural = rail->getContentSize();
        rail->setScaleX(kRailWidth   / std::max(1.f, natural.width));
        rail->setScaleY(barY * 2.f   / std::max(1.f, natural.height));
        rail->setPosition({sx * (halfW - kRailWidth / 2.f), 0.f});
        this->addChild(rail, 9);
    }

    // Las cuatro esquinas. El PNG se trata como esquina superior izquierda; las
    // otras tres son la misma imagen volteada. Si alguna sale espejada, basta con
    // invertir sus flags en esta tabla.
    struct Corner { float sx, sy; bool flipX, flipY; };
    const Corner corners[4] = {
        {-1.f,  1.f, false, false},   // arriba izquierda
        { 1.f,  1.f, true,  false},   // arriba derecha
        {-1.f, -1.f, false, true },   // abajo izquierda
        { 1.f, -1.f, true,  true },   // abajo derecha
    };

    for (auto const& c : corners) {
        auto spr = sheep::createOwn("corner.png"_spr);   // avisa en el log si falta
        if (!spr) break;

        const float scale = kCornerHeight / std::max(1.f, spr->getContentSize().height);
        spr->setScale(scale);
        spr->setFlipX(c.flipX);
        spr->setFlipY(c.flipY);

        // El ancho no se elige: sale del PNG y de esa escala. Se lee para colocar la
        // esquina, asi el calculo sigue valiendo aunque corner.png no sea cuadrado
        // o se reexporte a otra resolucion.
        const float cw = spr->getContentSize().width * scale;
        spr->setPosition({
            c.sx * (halfW + kCornerOut - cw / 2.f),
            c.sy * barY
        });
        this->addChild(spr, 11);
    }

    // Titulo, sobre la barra de arriba. Se crea una sola vez: si cambia el idioma,
    // no se actualiza hasta volver a entrar en la escena.
    auto title = CCLabelBMFont::create(sheep::tr("Queue_Title").c_str(), "bigFont.fnt");
    title->setScale(kTitleScale);
    title->setPosition({0.f, barY});
    this->addChild(title, 12);
}

// Ajusta el alto del lienzo interior al numero de celdas. Nunca menor que la
// ventana, o el scroll no tiene a donde moverse y se comporta de forma extrana.
void SheepQueuePanel::layoutContent(size_t count) {
    const float rowH = kCellHeight;
    const float totalH = std::max((float)count * rowH, m_listSize.height);
    m_scroll->m_contentLayer->setContentSize({m_listSize.width, totalH});
}

// Vacia la lista y muestra un texto centrado ("cargando", "cola vacia").
void SheepQueuePanel::showMessage(std::string const& text) {
    m_cells.clear();
    m_scroll->m_contentLayer->removeAllChildren();
    layoutContent(0);

    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->limitLabelWidth(m_listSize.width - 20.f, 0.45f, 0.25f);
    label->setOpacity(120);
    label->setPosition({m_listSize.width / 2.f, m_listSize.height / 2.f});
    m_scroll->m_contentLayer->addChild(label);

    m_scroll->moveToTop();
}

// Reconstruye la lista entera con la cola nueva, conservando la posicion del
// scroll si el usuario estaba mirando mas abajo.
void SheepQueuePanel::setQueue(std::vector<LevelModel> const& queue) {
    if (queue.empty()) {
        showMessage(sheep::tr("Queue_Empty"));
        m_queue = queue;      // la copia se actualiza tambien con la cola vacia
        return;
    }

    m_queue = queue;

    // Donde estaba mirando el usuario antes de reconstruir. Sin esto, cada
    // peticion nueva que entrase durante el directo lo devolveria arriba de golpe
    // mientras esta leyendo el final de la cola.
    const float prevY      = m_scroll->m_contentLayer->getPositionY();
    const float oldTotalH  = m_scroll->m_contentLayer->getContentSize().height;
    const bool  wasAtTop   = m_cells.empty() ||
                             prevY <= (m_listSize.height - oldTotalH) + 1.f;

    m_cells.clear();
    m_scroll->m_contentLayer->removeAllChildren();

    const size_t count = std::min(queue.size(), kMaxCells);
    layoutContent(count);

    const float totalH = m_scroll->m_contentLayer->getContentSize().height;

    // La primera fila arriba del todo: en cocos la Y crece hacia arriba.
    for (size_t i = 0; i < count; ++i) {
        auto cell = buildCell(queue[i], {m_listSize.width, kCellHeight}, i);
        cell->setPosition({0.f, totalH - (float)(i + 1) * kCellHeight});
        m_scroll->m_contentLayer->addChild(cell);
        m_cells.push_back(cell);
    }

    if (wasAtTop || totalH <= m_listSize.height) {
        m_scroll->moveToTop();
    }
    else {
        // Rango valido del lienzo: 'top' es el valor mas negativo (el contenido
        // asoma por arriba) y 0 es el final de la lista. La posicion solo se
        // restaura si no se ha ido al principio.
        const float top = m_listSize.height - totalH;
        m_scroll->m_contentLayer->setPositionY(std::clamp(prevY, top, 0.f));
    }

    if (queue.size() > kMaxCells) {
        log::warn("[SheepGD] {}", sheep::trf("Log_QueueTruncated",
                  queue.size(), kMaxCells));
    }
}

// Anima la salida de la fila 'index': sube y se pliega, y las de abajo suben un
// hueco. Devuelve false si el indice no existe.
bool SheepQueuePanel::animateRemoval(size_t index) {
    if (index >= m_cells.size()) return false;

    auto going = m_cells[index];

    // La fila que sale sube un hueco y se pliega a la vez. Se pliega en vez de
    // fundirse porque la opacidad no se propaga a los CCDrawNode del fondo, y la
    // escala funciona sobre cualquier nodo.
    going->runAction(CCSpawn::create(
        CCEaseSineIn::create(CCMoveBy::create(kAnimTime, {0.f, kCellHeight})),
        CCScaleTo::create(kAnimTime, 1.f, 0.f),
        nullptr
    ));

    // Las de abajo suben un hueco cada una.
    for (size_t i = index + 1; i < m_cells.size(); ++i) {
        m_cells[i]->runAction(
            CCEaseSineOut::create(CCMoveBy::create(kAnimTime, {0.f, kCellHeight}))
        );
    }

    // No se quitan aqui: cuando la animacion acaba, la escena llama a setQueue con
    // los datos ya actualizados y la lista se reconstruye entera. Como las
    // posiciones finales coinciden, el cambio no se nota.
    return true;
}

// Anima la entrada de una fila nueva arriba del todo: aparece desplegandose y las
// que ya estaban bajan un hueco.
//
// Nota: la fila nueva lleva el tag 0 y las demas no se renumeran, asi que hasta el
// siguiente setQueue los tags no coinciden con los indices de m_queue. La escena
// ignora los clics de las filas mientras dura la animacion por ese motivo.
bool SheepQueuePanel::animateInsertion(LevelModel const& level) {
    if (!m_scroll) return false;

    const float rowH = kCellHeight;

    // La fila nueva acabara justo donde esta ahora la primera.
    const float targetY = m_cells.empty()
        ? m_scroll->m_contentLayer->getContentSize().height - rowH
        : m_cells.front()->getPositionY();

    auto cell = buildCell(level, {m_listSize.width, rowH}, 0);
    cell->setPosition({0.f, targetY + rowH});   // un hueco por encima
    cell->setScaleY(0.f);                       // plegada
    m_scroll->m_contentLayer->addChild(cell);

    cell->runAction(CCSpawn::create(
        CCEaseSineOut::create(CCMoveTo::create(kAnimTime, {0.f, targetY})),
        CCScaleTo::create(kAnimTime, 1.f, 1.f),
        nullptr
    ));

    // Las que ya estaban bajan un hueco.
    for (auto c : m_cells) {
        c->runAction(CCEaseSineOut::create(CCMoveBy::create(kAnimTime, {0.f, -rowH})));
    }

    m_cells.insert(m_cells.begin(), cell);
    return true;
}

// Miniatura de una fila: de la cache si esta, y si no, se pide a la red y se pone
// cuando llega.
//
// Pasa por requestThumb, como la tarjeta: setQueue reconstruye todas las filas con
// cada cambio del JSON, y asi no se vuelve a pedir una miniatura que ya esta en
// camino ni una que fallo hace poco.
void SheepQueuePanel::attachThumb(CCNode* holder, CCSize const& size, LevelModel const& level) {
    if (level.levelId.empty()) return;

    if (auto tex = sheep::cachedThumb(level.levelId)) {
        addThumbSprite(holder, tex, size);
        return;
    }

    auto url = sheep::thumbUrl(level.levelId, level.thumbnailUrl);

    // Ref(holder) mantiene vivo el nodo hasta que llegue la respuesta. El
    // getParent() de despues detecta si la celda ya se reconstruyo (al destruirse,
    // la celda deja a sus hijos sin padre), y en ese caso no se dibuja nada.
    sheep::requestThumb(level.levelId, url,
        [ref = Ref(holder), size](CCTexture2D* tex) {
            if (tex && ref->getParent()) addThumbSprite(ref, tex, size);
        });
}

// Manejadores de los botones de las filas. El indice de la fila viaja en el tag
// del boton; con el se busca el nivel en m_queue y se avisa a la escena. El nivel
// se pasa por valor (ver los callbacks en SheepQueuePanel.hpp).
void SheepQueuePanel::handleCellPlay(CCObject* sender) {
    int i = static_cast<CCNode*>(sender)->getTag();
    if (i >= 0 && (size_t)i < m_queue.size() && onCellPlay) onCellPlay((size_t)i, m_queue[i]);
}

void SheepQueuePanel::handleCellView(CCObject* sender) {
    int i = static_cast<CCNode*>(sender)->getTag();
    if (i >= 0 && (size_t)i < m_queue.size() && onCellView) onCellView((size_t)i, m_queue[i]);
}

void SheepQueuePanel::handleCellDelete(CCObject* sender) {
    int i = static_cast<CCNode*>(sender)->getTag();
    if (i >= 0 && (size_t)i < m_queue.size() && onCellDelete) onCellDelete((size_t)i, m_queue[i]);
}

// Construye una fila. Es una version reducida de la tarjeta: miniatura con panel
// oscuro diagonal, cara y estrellas a la izquierda, nombre, autor, "Sent by" y
// estadisticas en el centro, y los botones View, papelera y Play a la derecha.
CCNode* SheepQueuePanel::buildCell(LevelModel const& level, CCSize const& size, size_t index) {
    auto cell = CCNode::create();
    cell->setContentSize(size);
    cell->setAnchorPoint({0.f, 0.f});

    // z0 · Base translucida: es lo que se ve mientras la miniatura no llega.
    auto base = CCDrawNode::create();
    CCPoint rect[4] = {{0.f, 0.f}, {size.width, 0.f}, {size.width, size.height}, {0.f, size.height}};
    base->drawPolygon(rect, 4, ccc4f(0.f, 0.f, 0.f, 0.30f), 0.f, ccc4f(0, 0, 0, 0));
    cell->addChild(base, 0);

    // z1 · Miniatura
    auto thumbHolder = CCNode::create();
    cell->addChild(thumbHolder, 1);
    attachThumb(thumbHolder, size, level);

    // z2 · Panel oscuro con corte diagonal, para que el texto se lea
    auto poly = CCDrawNode::create();
    CCPoint v[4] = {
        {0.f, 0.f},
        {size.width * kDarkBottom, 0.f},
        {size.width * kDarkTop, size.height},
        {0.f, size.height}
    };
    poly->drawPolygon(v, 4, ccc4f(0.f, 0.f, 0.f, 0.78f), 0.f, ccc4f(0, 0, 0, 0));
    cell->addChild(poly, 2);

    // z3 · Filo claro de 2 puntos sobre el corte diagonal
    auto edge = CCDrawNode::create();
    CCPoint e[4] = {
        {size.width * kDarkBottom,        0.f},
        {size.width * kDarkBottom + 2.f,  0.f},
        {size.width * kDarkTop + 2.f,     size.height},
        {size.width * kDarkTop,           size.height}
    };
    edge->drawPolygon(e, 4, ccc4f(1.f, 1.f, 1.f, 0.20f), 0.f, ccc4f(0, 0, 0, 0));
    cell->addChild(edge, 3);

    // z4 · Separador: una linea oscura de medio punto en el borde de arriba
    auto sep = CCDrawNode::create();
    CCPoint line[4] = {
        {0.f, size.height - 0.5f}, {size.width, size.height - 0.5f},
        {size.width, size.height}, {0.f, size.height}
    };
    sep->drawPolygon(line, 4, ccc4f(0.f, 0.f, 0.f, 0.55f), 0.f, ccc4f(0, 0, 0, 0));
    cell->addChild(sep, 4);

    // z5 · Columna izquierda: cara + estrellas
    int idx = sheep::difficultyIndex(level.difficultyName, level.stars);
    if (auto face = sheep::difficultyFaceNative(idx)) {
        face->updateFeatureState(sheep::featureState(level.featureGlowPath, level.hasFeatureGlow));
        face->setScale(0.46f);
        face->setPosition({kFaceX, size.height * 0.62f});
        cell->addChild(face, 5);
    }

    auto starsLabel = CCLabelBMFont::create(std::to_string(level.stars).c_str(), "bigFont.fnt");
    starsLabel->setAnchorPoint({1.f, 0.5f});
    starsLabel->setScale(0.30f);
    starsLabel->setPosition({kFaceX + 1.f, 9.f});
    cell->addChild(starsLabel, 5);

    if (auto st = sheep::iconStars(sheep::isPlatformer(level.length))) {
        st->setAnchorPoint({0.f, 0.5f});
        st->setScale(0.30f);
        st->setPosition({kFaceX + 4.f, 9.f});
        cell->addChild(st, 5);
    }

    // z5 · Columna central
    const float textMax = size.width - kBtnAreaW - kInfoX;

    auto name = CCLabelBMFont::create(level.name.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->limitLabelWidth(textMax * 0.62f, 0.42f, 0.22f);
    name->setPosition({kInfoX, kRowName});
    cell->addChild(name, 5);

    auto author = CCLabelBMFont::create(level.author.c_str(), "goldFont.fnt");
    author->setAnchorPoint({0.f, 0.5f});
    author->setColor({190, 180, 255});
    author->limitLabelWidth(textMax * 0.34f, 0.30f, 0.18f);
    // Justo detras del nombre: hay que medir lo que ocupa el nombre tras ajustarlo.
    author->setPosition({kInfoX + name->getScaledContentSize().width + 6.f, kRowName});
    cell->addChild(author, 5);

    if (auto pi = sheep::platformIcon(level.platformIconPath)) {
        pi->setAnchorPoint({0.f, 0.5f});
        pi->setScale(9.f / std::max(1.f, pi->getContentSize().height));
        pi->setPosition({kInfoX, kRowSentBy});
        cell->addChild(pi, 5);
    }

    auto sent = CCLabelBMFont::create(
        sheep::trf("Card_SentBy", level.requesterName).c_str(), "goldFont.fnt");
    sent->setAnchorPoint({0.f, 0.5f});
    sent->limitLabelWidth(textMax - 14.f, 0.30f, 0.18f);
    sent->setPosition({kInfoX + 13.f, kRowSentBy});
    cell->addChild(sent, 5);

    // Estadisticas: duracion, descargas y likes (sin monedas ni orbes, que solo
    // salen en la tarjeta). Si no caben, se encoge la fila.
    auto stats = CCNode::create();
    stats->setAnchorPoint({0.f, 0.5f});
    stats->setPosition({kInfoX, kRowStats});
    cell->addChild(stats, 5);

    std::vector<sheep::RowItem> items;
    if (!level.length.empty())    items.push_back({sheep::iconLength(),    level.length});
    if (!level.downloads.empty()) items.push_back({sheep::iconDownloads(), level.downloads});
    if (!level.likes.empty())     items.push_back({sheep::iconLikes(level.isDislike), level.likes});

    const float rowW = sheep::layoutIconTextRow(stats, items, 10.f, 0.28f, 4.f, 10.f);
    if (rowW > textMax && rowW > 0.f) stats->setScale(textMax / rowW);

    // z6 · Botones. El indice viaja en el tag: es lo unico que necesita el
    // manejador para saber de que fila viene el clic.
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    cell->addChild(menu, 6);

    const float cy = size.height / 2.f;

    constexpr float kCellBtn  = 26.f;   // alto de la papelera y del Play
    constexpr float kCellView = 20.f;   // alto del boton View

    if (auto spr = sheep::ownTextButton(sheep::tr("Btn_View").c_str(), kCellView, 0.4f, 10.f)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepQueuePanel::handleCellView));
        btn->setTag((int)index);
        btn->setPosition({size.width - 26.f, cy});
        menu->addChild(btn);
    }

    if (auto spr = sheep::ownSprite("trash.png"_spr, kCellBtn)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepQueuePanel::handleCellDelete));
        btn->setTag((int)index);
        btn->setPosition({size.width - 74.f, cy});
        menu->addChild(btn);
    }

    if (auto spr = sheep::ownSprite("play_btn.png"_spr, kCellBtn)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepQueuePanel::handleCellPlay));
        btn->setTag((int)index);
        btn->setPosition({size.width - 110.f, cy});
        menu->addChild(btn);
    }

    return cell;
}