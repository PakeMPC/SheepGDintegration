#include "SheepQueuePopup.hpp"
#include "SheepAssets.hpp"
#include "SheepSettingsPopup.hpp"
#include "SheepPlaceholder.hpp"
#include "SheepI18n.hpp"

using namespace cocos2d;
using namespace cocos2d::extension;

// ---------------------------------------------------------------------------
// SheepQueueLayer: la escena del mod. Junta la tarjeta del nivel actual
// (SheepNowPlayingCard), la lista de la cola (SheepQueuePanel), las flechas de
// anterior y siguiente, los botones de volver y de ajustes y los dos Sheepys de
// las esquinas.
//
// Los datos no viven aqui sino en SheepData: la escena se suscribe y repinta con
// applyData cada vez que cambian. Los botones mutan la cola, la guardan
// (persistAndRefresh) y el aviso de SheepData es el que repinta.
//
// Tambien lanza niveles (View y Play) con su propia busqueda y descarga, y espera
// a la musica antes de entrar, como el lanzador de la partida.
// ---------------------------------------------------------------------------

bool SheepQueueLayer::s_returnTicket = false;

// Crea la capa. fadeIn = true cuando se vuelve de un nivel: la escena entra con un
// velo negro que se abre (ver init).
SheepQueueLayer* SheepQueueLayer::create(bool fadeIn) {
    auto ret = new SheepQueueLayer();
    if (ret) {
        // El flag se asigna antes de init(), porque es init() quien monta el velo.
        // Por eso no puede ir como parametro de init(), que es el de CCLayer.
        ret->m_fadeIn = fadeIn;
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// Una escena nueva con la capa dentro, lista para pushScene o replaceScene.
CCScene* SheepQueueLayer::scene(bool fadeIn) {
    auto scene = CCScene::create();
    scene->addChild(SheepQueueLayer::create(fadeIn));
    return scene;
}

// Al destruirse: darse de baja de SheepData (obligatorio, o el servicio llamaria a
// una capa liberada), soltar el nivel retenido y los huecos del delegado.
SheepQueueLayer::~SheepQueueLayer() {
    if (m_subId) SheepData::get()->unsubscribe(m_subId);
    if (m_waitLevel) { m_waitLevel->release(); m_waitLevel = nullptr; }
    clearDelegates();
}

// Suelta los huecos del delegado de GameLevelManager, solo si los ocupa esta capa.
void SheepQueueLayer::clearDelegates() {
    auto glm = GameLevelManager::sharedState();
    if (!glm) return;
    if (glm->m_levelManagerDelegate  == this) glm->m_levelManagerDelegate  = nullptr;
    if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
}

// Monta la escena: fondo, botones de volver y de ajustes, tarjeta, flechas, lista
// y Sheepys; activa la escucha de teclado, raton y toques; se suscribe a SheepData
// y, si viene de un nivel, pone el velo de entrada.
bool SheepQueueLayer::init() {
    if (!CCLayer::init()) return false;

    // El idioma se relee en cada visita a la escena: asi, cambiarlo en
    // SheepGDRequest y volver a entrar basta para verlo aplicado.
    sheep::refreshLanguage(QueueData::getLinkedFilePath());

    // Si ya estamos en la escena, el billete de vuelta sobra. Esto cubre el caso de
    // que alguna version de GD si desapile y vuelva sola: sin esto, el billete se
    // quedaria puesto y la siguiente partida normal se trataria como si viniera
    // de la cola.
    s_returnTicket = false;

    auto winSize = CCDirector::get()->getWinSize();

    // 1. Fondo propio
    if (auto bg = sheep::createOwn("sheep_bg.png"_spr)) {
        auto bgSize = bg->getContentSize();

        // Escala "cover": uniforme, para llenar la pantalla sin deformar el
        // patron. Con setScaleX/Y por separado, una pantalla de otra proporcion
        // estiraria las ovejas.
        float scale = std::max(winSize.width / bgSize.width,
                               winSize.height / bgSize.height);
        bg->setScale(scale);
        bg->setPosition({winSize.width / 2.f, winSize.height / 2.f});
        this->addChild(bg, -1);
    }

    // Boton de volver, arriba a la izquierda.
    auto backMenu = CCMenu::create();
    backMenu->setPosition({25.f, winSize.height - 25.f});
    this->addChild(backMenu, 30);

    // arrow.png apunta a la derecha, asi que la de "atras" va volteada.
    if (auto spr = sheep::ownSprite("arrow.png"_spr, 34.f)) {
        spr->setFlipX(true);
        backMenu->addChild(CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepQueueLayer::onBack)));
    }

    // Boton de ajustes: espejo del de volver, en la esquina opuesta.
    auto setMenu = CCMenu::create();
    setMenu->setPosition({winSize.width - 25.f, winSize.height - 25.f});
    this->addChild(setMenu, 30);

    if (auto spr = sheep::ownSprite("settings.png"_spr, 34.f)) {
        setMenu->addChild(CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(SheepQueueLayer::onSettings)));
    }

    createTopPanel();
    createNavArrows();
    createBottomPanel();

    createBouncers();

    // Escucha propia. En el fork de RobTop, CCLayer ya incluye los delegados de
    // teclado y de raton, asi que no hace falta tocar el hook global del teclado.
    this->setKeypadEnabled(true);
    this->setKeyboardEnabled(true);   // espacio y flecha arriba
    this->setMouseEnabled(true);      // clic derecho
    this->setTouchEnabled(true);      // clic izquierdo

    // La suscripcion va antes de tocar la ruta: setPath fuerza una lectura y avisa
    // en el acto, y la escena tiene que estar escuchando cuando lo haga.
    m_subId = SheepData::get()->subscribe([this](QueueData const&) {
        this->applyData();
    });

    auto svc = SheepData::get();
    if (svc->hasPath()) {
        svc->start();      // por si el sondeo estaba parado
        applyData();       // el servicio ya tiene datos: se pinta sin esperar
    }
    else if (QueueData::hasLinkedFile()) {
        svc->setPath(QueueData::getLinkedFilePath());
    }
    else {
        promptForDataPath();
    }

    // Fundido de entrada propio: un velo negro a pantalla completa que se
    // desvanece. No se usa CCTransitionScene porque necesita una escena saliente
    // viva para animar, y al volver de un nivel la del PlayLayer ya esta
    // desmontada.
    if (m_fadeIn) {
        auto veil = CCLayerColor::create({0, 0, 0, 255});
        veil->setContentSize(winSize);
        this->addChild(veil, 9999);   // por encima de todo

        veil->runAction(CCSequence::create(
            CCFadeTo::create(0.4f, 0),
            CCCallFuncN::create(this, callfuncN_selector(SheepQueueLayer::onVeilDone)),
            nullptr
        ));
    }

    return true;

}

// Final del velo de entrada. CCCallFuncN recibe el nodo que ejecuta la accion, es
// decir, el propio velo.
void SheepQueueLayer::onVeilDone(CCNode* veil) {
    if (veil) veil->removeFromParent();
}

// Vuelve al menu principal. La escena se abrio con pushScene, asi que se sale con
// pop.
void SheepQueueLayer::onBack(CCObject*) {
    clearDelegates();
    CCDirector::get()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

void SheepQueueLayer::onSettings(CCObject*) {
    if (auto p = SheepSettingsPopup::create()) p->show();
}

// "Atras" del delegado de keypad; equivale a la flecha de volver. (El Esc del
// teclado se trata aparte, en keyDown.)
void SheepQueueLayer::keyBackClicked() {
    onBack(nullptr);
}

// ---------------------------------------------------------------------------
// Interfaz
// ---------------------------------------------------------------------------

// La tarjeta del nivel actual, con la cabeza de oveja encima, y sus callbacks.
void SheepQueueLayer::createTopPanel() {
    auto winSize = CCDirector::get()->getWinSize();

    const float cardW = winSize.width - 150.f;
    const float cardH = 118.f;
    const float cardY = winSize.height - 90.f;   // 20 mas abajo, para dejar sitio a la cabeza

    m_card = SheepNowPlayingCard::create({cardW, cardH});
    m_card->setPosition({winSize.width / 2, cardY});
    this->addChild(m_card, 10);

    // La cabeza es hija de la tarjeta, no de la capa: asi se desliza con ella y
    // sus coordenadas son locales (no dependen de donde este la tarjeta).
    if (auto head = sheep::createOwn("sheep_head.png"_spr)) {
        constexpr float kHeadHeight  = 34.f;
        constexpr float kHeadOverlap = 6.f;   // cuanto se apoya sobre el borde de la tarjeta

        head->setScale(kHeadHeight / std::max(1.f, head->getContentSize().height));
        head->setAnchorPoint({0.5f, 0.f});
        head->setPosition({cardW / 2.f, cardH - kHeadOverlap});
        m_card->addChild(head, 100);   // por delante de todo lo de la tarjeta
    }

    // View: abre la pagina del nivel en GD.
    m_card->onView = [this](LevelModel const& lv) {
        int id = lv.levelIdAsInt();
        if (id <= 0) return;
        requestLevel(id, false);
    };

    // Play: lanza el nivel actual (o el placeholder, si es lo que se muestra).
    m_card->onPlay = [this](LevelModel const& lv) {
        int id = lv.levelIdAsInt();
        if (id <= 0) return;

        // Si lo que se lanza es el placeholder, hay que indicarlo: el vigia de la
        // partida usa esa bandera para no volver a lanzarlo en bucle y para saber
        // que cualquier peticion nueva le vale.
        sheep::inPlaceholder() = m_showingPlaceholder;
        requestLevel(id, true);
    };

    // Papelera: descarta el nivel actual (sin pasar por el historial), con
    // confirmacion.
    m_card->onDelete = [this](LevelModel const&) {
        // En variables locales y no dentro de la llamada: createQuickPopup recibe
        // char const*, asi que el c_str() de un tr() suelto apuntaria a un temporal
        // ya destruido.
        auto titulo  = sheep::tr("Del_Title");
        auto cuerpo  = sheep::tr("Del_Body");
        auto cancel  = sheep::tr("Del_Cancel");
        auto aceptar = sheep::tr("Del_Confirm");

        geode::createQuickPopup(
            titulo.c_str(),
            cuerpo.c_str(),
            cancel.c_str(), aceptar.c_str(),
            [this](FLAlertLayer*, bool btn2) {
                if (!btn2) return;
                data().discardCurrent();
                persistAndRefresh();
            }
        );
    };

    // Dado: elige un nivel al azar de la cola como actual.
    m_card->onRandom = [this]() {
        if (data().queue.empty()) {
            FLAlertLayer::create("SheepGD", sheep::tr("Alert_QueueEmpty").c_str(),
                                 sheep::tr("Alert_Ok").c_str())->show();
            return;
        }
        data().pickRandom();
        persistAndRefresh();
    };
}

// Desliza la tarjeta: sale por un lado y vuelve por el otro. El cambio de datos se
// aplica en el punto medio (onCardSlideMid), con la tarjeta fuera de la pantalla.
void SheepQueueLayer::slideCard(bool toLeft) {
    if (!m_card) return;

    auto winSize = CCDirector::get()->getWinSize();
    const float y = m_card->getPositionY();

    // Al pulsar "siguiente" la tarjeta sale por la izquierda y la nueva entra por la
    // derecha: el movimiento sigue la direccion de lectura.
    const float outX = toLeft ? -winSize.width : winSize.width * 2.f;
    m_pendingSlideInX = toLeft ?  winSize.width * 2.f : -winSize.width;

    m_card->stopAllActions();
    m_card->runAction(CCSequence::create(
        CCEaseSineIn::create(CCMoveTo::create(0.15f, {outX, y})),
        CCCallFunc::create(this, callfunc_selector(SheepQueueLayer::onCardSlideMid)),
        nullptr
    ));
}

// Punto medio del deslizamiento: la tarjeta, fuera de pantalla, cambia de nivel y
// vuelve a entrar por el lado contrario.
void SheepQueueLayer::onCardSlideMid() {
    if (!m_card) return;

    auto winSize = CCDirector::get()->getWinSize();
    const float y = m_card->getPositionY();

    // Salto al lado contrario y cambio de contenido, todo fuera de la pantalla y en
    // el mismo frame. Con paintCard y no con setLevel a secas: si antes se mostraba
    // el placeholder, el modo placeholder de la tarjeta tambien tiene que cambiar.
    paintCard();
    m_card->setPositionX(m_pendingSlideInX);

    m_card->runAction(CCSequence::create(
        // BackOut se pasa un poco del destino y vuelve: es el pequeno rebote que
        // usa GD, y evita que parezca un deslizamiento plano.
        CCEaseBackOut::create(CCMoveTo::create(0.25f, {winSize.width / 2.f, y})),
        CCCallFunc::create(this, callfunc_selector(SheepQueueLayer::onCardSlideDone)),
        nullptr
    ));
}

// Final de una transicion (deslizamiento, borrado o Play de una fila): baja
// m_cardSliding y repinta la tarjeta y la lista con los datos actuales, que
// applyData no pudo pintar mientras la transicion estaba en curso.
void SheepQueueLayer::onCardSlideDone() {
    m_cardSliding = false;
    applyData();
}

// Flechas de nivel anterior y siguiente, a los lados de la tarjeta.
void SheepQueueLayer::createNavArrows() {
    auto winSize = CCDirector::get()->getWinSize();
    float y = winSize.height - 78.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    this->addChild(menu, 20);

    constexpr float kArrowH = 46.f;

    // Dos llamadas a ownSprite, no un sprite reutilizado: un CCSprite solo puede
    // tener un padre, asi que el mismo objeto no vale para los dos botones.
    if (auto spr = sheep::ownSprite("arrow.png"_spr, kArrowH)) {
        spr->setFlipX(true);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepQueueLayer::onPrevLevel));
        btn->setPosition({40.f, y});
        menu->addChild(btn);
    }

    if (auto spr = sheep::ownSprite("arrow.png"_spr, kArrowH)) {
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SheepQueueLayer::onNextLevel));
        btn->setPosition({winSize.width - 40.f, y});
        menu->addChild(btn);
    }
}

// La lista de la cola y los callbacks de sus filas.
void SheepQueueLayer::createBottomPanel() {
    auto winSize = CCDirector::get()->getWinSize();

    // 126 = dos filas de 52 mas un 40% de la tercera asomando, que es lo que deja
    // claro que la lista se desplaza.
    m_queuePanel = SheepQueuePanel::create({winSize.width - 178.f, 126.f});

    // El panel tiene el ancla en el centro. Su marco (las barras de
    // SheepQueuePanel) sobresale unos 15 puntos por arriba y por abajo de la lista,
    // asi que ocupa aproximadamente de y = 7 a y = 163.
    m_queuePanel->setPosition({winSize.width / 2.f, 85.f});
    this->addChild(m_queuePanel, 10);

    // View de una fila: abre la pagina del nivel en GD.
    m_queuePanel->onCellView = [this](size_t, LevelModel lv) {
        // Misma guarda que Play y la papelera. Mientras la lista se anima, los tags
        // de las filas no coinciden con los indices reales: animateInsertion mete
        // la fila nueva con indice 0 y no renumera las que ya estaban, asi que
        // durante esos ~0,4 s hay dos filas con tag 0 y el resto van desplazadas.
        // Sin esta linea, un clic aqui abriria el nivel de al lado. La lista se
        // reconstruye entera al terminar la animacion (onCardSlideDone ->
        // setQueue), y entonces los tags vuelven a cuadrar.
        if (m_cardSliding) return;

        int id = lv.levelIdAsInt();
        if (id > 0) requestLevel(id, false);
    };

    // Play de una fila: ese nivel pasa a ser el actual y se lanza.
    m_queuePanel->onCellPlay = [this](size_t index, LevelModel lv) {
        if (m_cardSliding) return;

        // Misma guarda que el borrado, y por el mismo motivo: persistAndRefresh
        // acaba en applyData -> setQueue -> removeAllChildren sobre el contenedor
        // del scroll, y este codigo se ejecuta dentro del callback del boton de una
        // fila. Reconstruir la lista aqui destruiria el nodo que esta despachando
        // el toque en ese momento.
        //
        // Antes no fallaba porque el CCTouchDispatcher de cocos aplaza las bajas
        // mientras despacha y mantiene el menu vivo un frame mas, pero eso es un
        // detalle interno suyo, no una garantia.
        m_cardSliding = true;

        data().promoteFromQueue(index);
        persistAndRefresh();

        int id = lv.levelIdAsInt();
        if (id > 0) requestLevel(id, true);

        // La bandera se baja siempre un frame despues, no solo si el lanzamiento
        // sale bien: si el nivel no existe o falla la red, la escena sigue en
        // pantalla y tiene que quedar usable. onCardSlideDone es ademas quien hace
        // la reconstruccion que se acaba de aplazar.
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.f),
            CCCallFunc::create(this, callfunc_selector(SheepQueueLayer::onCardSlideDone)),
            nullptr
        ));
    };

    // Papelera de una fila: la quita de la cola con animacion.
    m_queuePanel->onCellDelete = [this](size_t index, LevelModel lv) {
        if (m_cardSliding) return;

        // Se reutiliza la bandera de transicion para que applyData no reconstruya la
        // lista a mitad de la animacion.
        m_cardSliding = true;
        m_queuePanel->animateRemoval(index);
        data().removeFromQueue(lv.requestId);
        persistAndRefresh();

        // Aqui no hay tarjeta deslizandose que cierre la transicion, asi que se
        // cierra cuando acaba la animacion de la fila.
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.28f),
            CCCallFunc::create(this, callfunc_selector(SheepQueueLayer::onCardSlideDone)),
            nullptr
        ));
    };
}

// ---------------------------------------------------------------------------
// Datos
// ---------------------------------------------------------------------------

// Pide al usuario que enlace queue_data.json. Si cancela, la lista lo indica.
void SheepQueueLayer::promptForDataPath() {
    QueueData::promptForFile(
        // No hace falta capturar 'this': setPath avisa a todos los suscriptores, y
        // la escena es uno de ellos.
        [](std::filesystem::path const& path) {
            SheepData::get()->setPath(path);
        },
        [self = Ref(this)]() {
            if (self->m_queuePanel) self->m_queuePanel->showMessage(sheep::tr("Queue_NoFile"));
        }
    );
}

// Repinta la tarjeta y la lista con los datos de SheepData. Es el callback de la
// suscripcion, y el unico sitio que pinta las dos a partir de los datos.
void SheepQueueLayer::applyData() {
    // El filtro de "algo cambio" lo hace el servicio: si llega aqui, algo cambio.
    // Lo unico que queda es la razon que el servicio no puede conocer: una
    // transicion en curso, que repintara al terminar (onCardSlideDone).
    if (m_cardSliding) return;

    paintCard();
    if (m_queuePanel) m_queuePanel->setQueue(SheepData::get()->data().queue);
}

// Pone en la tarjeta el nivel actual, o el placeholder si toca. Aparte de applyData
// porque el punto medio de un deslizamiento tambien la pinta, y los dos tienen que
// decidir igual.
void SheepQueueLayer::paintCard() {
    auto const& d = SheepData::get()->data();

    // La tarjeta muestra el placeholder cuando la cola se acabo y ese es el modo
    // elegido. Si la ficha todavia no ha llegado, se sigue viendo el WAITING de
    // siempre y ya cambiara: fetch avanza la busqueda un paso.
    LevelModel const* mostrar = nullptr;
    if (sheep::wantsPlaceholderCard(d))
        mostrar = SheepPlaceholder::get()->fetch(sheep::placeholderLevelId());

    m_showingPlaceholder = (mostrar != nullptr);

    if (m_card) {
        m_card->setPlaceholder(m_showingPlaceholder);   // antes de setLevel
        m_card->setLevel(m_showingPlaceholder ? *mostrar : d.current);
    }
}

// Guarda la cola despues de mutarla. El repintado no se hace aqui: lo provoca el
// aviso de SheepData::save(). Si la escritura falla, save() deshace la mutacion y
// avisa con los datos de antes, asi que la escena vuelve a lo que dice el archivo.
//
// Sin ruta la mutacion se queda en memoria, pero sin ruta tampoco hay cola que
// mutar: los datos estan vacios.
void SheepQueueLayer::persistAndRefresh() {
    auto svc = SheepData::get();
    if (!svc->hasPath()) {
        log::warn("[SheepGD] {}", sheep::tr("Log_NoLinkedPath"));
        return;
    }
    // save() escribe, apunta su propia fecha para no releerse a si mismo y avisa a
    // los suscriptores. El refresco de la interfaz viene de ahi.
    if (!svc->save()) {
        FLAlertLayer::create("SheepGD", sheep::tr("Alert_WriteFailed").c_str(),
                             sheep::tr("Alert_Ok").c_str())->show();
    }
}

// Flecha izquierda: vuelve al nivel anterior del historial.
void SheepQueueLayer::onPrevLevel(CCObject*) {
    if (m_cardSliding || data().history.empty()) return;

    // Enfriamiento de un segundo (sheep::trySkip). m_cardSliding ya frena durante la
    // animacion, pero son ~0,4 s y solo cubren esta escena; el limite real es este,
    // y es el mismo que usan las teclas Q/E en la partida. Va antes de levantar la
    // bandera para no dejarla puesta en un salto que no va a ocurrir.
    if (!sheep::trySkip()) return;

    m_cardSliding = true;

    // El nivel actual vuelve al frente de la cola, asi que la lista gana una fila.
    // Se anima antes de mutar porque data().current es justo el que va a entrar.
    if (m_queuePanel && data().current.isValid())
        m_queuePanel->animateInsertion(data().current);

    data().backToPrevious();
    persistAndRefresh();
    slideCard(false);
}

// Flecha derecha: pasa al siguiente nivel de la cola.
void SheepQueueLayer::onNextLevel(CCObject*) {
    if (m_cardSliding || data().queue.empty()) return;

    if (!sheep::trySkip()) return;   // enfriamiento, igual que onPrevLevel

    m_cardSliding = true;

    // La animacion se lanza antes de mutar los datos, porque anima las filas que
    // hay ahora mismo en pantalla.
    if (m_queuePanel) m_queuePanel->animateRemoval(0);

    data().skipToNext();
    persistAndRefresh();
    slideCard(true);
}

// ---------------------------------------------------------------------------
// Integracion con GD (View / Play)
// ---------------------------------------------------------------------------

// Muestra u oculta la rueda de carga del juego.
void SheepQueueLayer::showLoading(bool visible) {
    if (visible) {
        if (m_loading) return;
        m_loading = LoadingCircle::create();
        m_loading->setParentLayer(this);
        m_loading->show();
    } else if (m_loading) {
        m_loading->fadeAndRemove();
        m_loading = nullptr;
    }
}

// Busca el nivel en los servidores y despues abre su pagina (View) o lo descarga y
// lo lanza (Play). Usa la cache de busquedas de GD si la hay.
//
void SheepQueueLayer::requestLevel(int levelId, bool playAfter) {
    m_pendingLevelId = levelId;
    m_playAfterFetch = playAfter;

    // Desde aqui, y no mas abajo: el camino de la cache llama al manejador
    // directamente, sin pasar por la red.
    m_searching = true;

    // La escena va a ocupar el hueco del delegado, asi que primero avisa a los dos
    // de segundo plano para que se aparten, igual que SheepLevelLauncher::play.
    // Quitarselo sin avisar tambien funciona, pero los deja esperando una respuesta
    // que ya no les va a llegar: el buscador del placeholder no tiene watchdog que
    // lo saque de ahi, y al precargador la espera le gasta un intento.
    //
    // Antes de la cache y no despues: aunque la busqueda salga de la cache, Play
    // ocupa despues el hueco de descargas.
    SheepPlaceholder::get()->yield();
    SheepPrefetcher::get()->yield();

    auto glm = GameLevelManager::sharedState();
    auto searchObj = GJSearchObject::create(SearchType::Search, std::to_string(levelId));

    // Si la busqueda ya esta en cache, se evita el viaje al servidor.
    if (auto stored = glm->getStoredOnlineLevels(searchObj->getKey())) {
        loadLevelsFinished(stored, "");
        return;
    }

    glm->m_levelManagerDelegate = this;
    showLoading(true);
    glm->getOnlineLevels(searchObj);
}

// Abre la pagina del nivel de GD (LevelInfoLayer) encima de la escena.
void SheepQueueLayer::openLevelInfo(GJGameLevel* level) {
    if (!level) return;
    auto scene = CCScene::create();
    scene->addChild(LevelInfoLayer::create(level, false));
    CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
}

// Resultado de la busqueda de requestLevel.
void SheepQueueLayer::loadLevelsFinished(CCArray* levels, char const*) {
    // Primero, comprobar si la respuesta es propia. El hueco del delegado es unico,
    // asi que aqui puede llegar la respuesta de otro.
    if (!m_searching) return;
    m_searching = false;

    showLoading(false);
    clearDelegates();

    GJGameLevel* found = nullptr;
    if (levels) {
        for (unsigned int i = 0; i < levels->count(); ++i) {
            auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if (lvl && lvl->m_levelID == m_pendingLevelId) { found = lvl; break; }
        }
    }

    // Si el nivel no esta entre los resultados, no se toma otro en su lugar: la
    // busqueda de GD por un numero tambien trae niveles cuyo nombre lo contiene, y
    // Play lanzaria uno cualquiera.
    if (!found) {
        FLAlertLayer::create("SheepGD", sheep::tr("Alert_NotFound").c_str(),
                             sheep::tr("Alert_Ok").c_str())->show();
        return;
    }

    if (!m_playAfterFetch) {
        openLevelInfo(found);
        return;
    }

    // Play: hay que descargar el contenido del nivel antes de crear el PlayLayer.
    auto glm = GameLevelManager::sharedState();
    glm->m_levelDownloadDelegate = this;
    m_downloading = true;
    showLoading(true);
    glm->downloadLevel(found->m_levelID, false, 0);
}

// Variante con tipo que declara el delegado; hace lo mismo.
void SheepQueueLayer::loadLevelsFinished(CCArray* levels, char const* key, int) {
    loadLevelsFinished(levels, key);
}

// Fallo de la busqueda de requestLevel.
void SheepQueueLayer::loadLevelsFailed(char const*) {
    // Aqui llega el codigo de error, no el id, asi que no hay nada que comparar: lo
    // unico que se sabe es si la busqueda era propia. Sin esta linea, un fallo del
    // buscador del placeholder o del precargador (que comparten el hueco) mostraba
    // este aviso encima de una escena que no habia pedido nada.
    if (!m_searching) return;
    m_searching = false;

    showLoading(false);
    clearDelegates();
    FLAlertLayer::create("SheepGD", sheep::tr("Alert_SearchFailed").c_str(),
                         sheep::tr("Alert_Ok").c_str())->show();
}

void SheepQueueLayer::loadLevelsFailed(char const* key, int) {
    loadLevelsFailed(key);
}

// Nivel descargado (Play): pide su audio y lo lanza en cuanto la musica este lista,
// o a los kAudioWaitMax segundos.
void SheepQueueLayer::levelDownloadFinished(GJGameLevel* level) {
    if (!m_downloading) return;   // la descarga es de otro

    if (!level) { m_downloading = false; showLoading(false); clearDelegates(); return; }

    // m_levelDownloadDelegate es un hueco unico y compartido. Si lo que llega no es
    // lo que se pidio, es de otro: no se hace nada y se sigue registrado, para que
    // la descarga propia llegue cuando toque. Soltar el delegado aqui dejaria la
    // escena esperando para siempre.
    if (m_pendingLevelId > 0 && level->m_levelID != m_pendingLevelId) return;

    m_downloading = false;
    clearDelegates();

    if (m_waitLevel) m_waitLevel->release();
    m_waitLevel = level;
    m_waitLevel->retain();

    m_waitFallbackSongId = fallbackForLevel(level->m_levelID);

    // Pedir antes de mirar: si este nivel no era objetivo del precargador, nadie ha
    // pedido su audio todavia y esperar no pondria nada en marcha.
    sheep::ensureLevelAudio(m_waitLevel, m_waitFallbackSongId);

    if (sheep::levelAudioReady(m_waitLevel, m_waitFallbackSongId)) {
        launchWaitedLevel();
        return;
    }

    // La rueda de carga se queda. Aqui, al contrario que en el auto-skip, el usuario
    // acaba de pulsar y esta mirando: una rueda girando es justo lo que hay que
    // mostrarle mientras termina de bajar la musica. El sondeo va en el schedule de
    // la capa: si el usuario sale a mitad de la espera, se va con ella.
    m_audioWaited = 0.f;
    this->schedule(schedule_selector(SheepQueueLayer::audioWaitTick), sheep::kAudioPoll);
}

// Sondeo de la espera de musica, cada kAudioPoll segundos. Solo cuentan las
// canciones: los SFX se piden igual, pero no se esperan.
void SheepQueueLayer::audioWaitTick(float dt) {
    if (!m_waitLevel) { stopAudioWait(); showLoading(false); return; }

    m_audioWaited += dt;

    // Se empuja en cada vuelta: ensureSong y ensureSfx avanzan un paso por llamada.
    // Cuando ya no queda nada que hacer, la llamada es barata.
    sheep::ensureLevelAudio(m_waitLevel, m_waitFallbackSongId);

    bool ready = sheep::levelAudioReady(m_waitLevel, m_waitFallbackSongId);
    if (!ready && m_audioWaited < sheep::kAudioWaitMax) return;

    // .value(): m_levelID es un SeedValueRSV y fmt no sabe formatearlo.
    if (ready) log::debug("[SheepGD] {}", sheep::trf("Log_MusicReady",
                          m_waitLevel->m_levelID.value(), m_audioWaited));
    else       log::warn("[SheepGD] {}", sheep::trf("Log_MusicTimeout",
                         m_waitLevel->m_levelID.value()));

    launchWaitedLevel();
}

// Lanza el nivel descargado. Marca el billete de vuelta, para que la partida sepa
// que viene de la cola y al salir vuelva a esta escena.
void SheepQueueLayer::launchWaitedLevel() {
    stopAudioWait();
    showLoading(false);
    if (!m_waitLevel) return;

    s_returnTicket = true;

    // replaceScene y no pushScene: al salir del nivel se vuelve con otro
    // replaceScene, asi que si aqui se apilara, cada ida y vuelta dejaria una
    // escena del mod enterrada en la pila. Push y pop tienen que ir siempre en
    // pareja; aqui la pareja natural es replace/replace.
    auto scene = PlayLayer::scene(m_waitLevel, false, false);
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));

    m_waitLevel->release();
    m_waitLevel = nullptr;
}

void SheepQueueLayer::stopAudioWait() {
    this->unschedule(schedule_selector(SheepQueueLayer::audioWaitTick));
}

// El CustomSongId del JSON para ese nivel, buscado en el actual y en la cola. Solo
// se usa si el nivel no trae m_songIDs.
int SheepQueueLayer::fallbackForLevel(int levelId) {
    auto const& d = data();
    if (d.current.levelIdAsInt() == levelId) return d.current.customSongIdAsInt();
    for (auto const& lv : d.queue)
        if (lv.levelIdAsInt() == levelId) return lv.customSongIdAsInt();
    return 0;
}

// Fallo de la descarga.
void SheepQueueLayer::levelDownloadFailed(int) {
    // Igual que en la busqueda: llega el codigo, no el id. Si la escena no tenia una
    // descarga en marcha, el fallo es de otro (el caso tipico es el precargador al
    // que se le acaba de quitar el hueco), y cancelar por eso dejaria al usuario sin
    // su nivel por un error ajeno.
    if (!m_downloading) return;
    m_downloading = false;

    showLoading(false);
    clearDelegates();
    FLAlertLayer::create("SheepGD", sheep::tr("Alert_DownloadFailed").c_str(),
                         sheep::tr("Alert_Ok").c_str())->show();
}

// ---------------------------------------------------------------------------
// Los dos Sheepys de las esquinas. Son decorativos.
// ---------------------------------------------------------------------------

// Crea los dos Sheepys y arranca el tick de la escena.
void SheepQueueLayer::createBouncers() {
    auto win = CCDirector::get()->getWinSize();

    // El panel de la lista mide win.width - 178, asi que a cada lado quedan unos
    // 89 puntos libres; 46 es el centro de ese hueco.
    constexpr float kX = 46.f;
    constexpr float kY = 46.f;

    // Se crean siempre, aunque la opcion este apagada: asi, encenderla desde el
    // popup de ajustes (que se abre desde esta misma escena) los hace aparecer sin
    // tener que salir y volver a entrar. De la visibilidad se encarga el tick.
    m_bounceP1 = SheepBouncer::create(false);
    if (m_bounceP1) {
        m_bounceP1->setPosition({kX, kY});
        this->addChild(m_bounceP1, 5);
    }

    m_bounceP2 = SheepBouncer::create(true);
    if (m_bounceP2) {
        m_bounceP2->setPosition({win.width - kX, kY});
        this->addChild(m_bounceP2, 5);
    }

    this->schedule(schedule_selector(SheepQueueLayer::sceneTick), 0.5f);
    sceneTick(0.f);
}

// Tick de la escena, cada 0,5 s: visibilidad y colores de los Sheepys, y vigilancia
// de la tarjeta del placeholder.
void SheepQueueLayer::sceneTick(float) {
    // "Only in requests" no se mira aqui, al contrario que en el taller: esta es la
    // escena de las peticiones, y quien activa el cubo de Sheepy espera verlo aqui.
    bool on = sheep::sheepyEnabled();

    if (m_bounceP1) { m_bounceP1->setVisible(on); if (on) m_bounceP1->refreshColors(); }
    if (m_bounceP2) { m_bounceP2->setVisible(on); if (on) m_bounceP2->refreshColors(); }

    // La tarjeta del placeholder no la puede activar applyData, que solo corre
    // cuando cambia el JSON: sus dos transiciones ocurren sin que el JSON cambie
    // (un plazo que vence por el reloj, y la ficha que llega de la red). Asi que el
    // cambio se vigila aqui, y el repintado sigue siendo de applyData, que es el
    // unico sitio que toca la tarjeta y la lista.
    bool quiere = sheep::wantsPlaceholderCard(SheepData::get()->data());
    bool hay    = quiere && SheepPlaceholder::get()->fetch(sheep::placeholderLevelId()) != nullptr;

    if (hay != m_showingPlaceholder) applyData();
}

// Brinco del Sheepy del jugador 1 (izquierda) o del 2 (derecha), si se ve.
void SheepQueueLayer::bounce(bool player2) {
    auto b = player2 ? m_bounceP2 : m_bounceP1;
    if (b && b->isVisible()) b->hop();
}

// Teclado de la escena: Esc sale, espacio y flecha arriba hacen brincar a los
// Sheepys.
void SheepQueueLayer::keyDown(cocos2d::enumKeyCodes key, double) {
    // Esc sale al menu principal, lo mismo que la flecha de arriba a la izquierda.
    // Se trata aqui y no se confia en que CCLayer lo traduzca a keyBackClicked: este
    // override no llama al de la clase base (es un callback de delegado y no se
    // quiere depender de un simbolo que quiza no este exportado), asi que esa
    // cadena podria no completarse.
    if (key == cocos2d::KEY_Escape) {
        // Con un popup abierto, el Esc es del popup. El despachador avisa a todos
        // los delegados y keyDown devuelve void, asi que no hay forma de consumir la
        // tecla: un solo Esc cerraria el popup y ademas sacaria de la escena. Hay
        // que mirar el estado.
        if (!hasPopupOpen()) onBack(nullptr);
        return;
    }

    // Los controles de GD 2.2, comprobados jugando el 2026-09-02. No hay tabla de
    // teclas que consultar: GJBaseGameLayer::handleButton solo existe dentro de una
    // capa de juego, y esta escena no lo es.
    if (key == cocos2d::KEY_Space) bounce(false);
    else if (key == cocos2d::KEY_Up) bounce(true);
}

// Indica si hay algun FLAlertLayer por encima de la escena. Cubre de una vez el
// popup de ajustes, el de confirmar el borrado y el de enlazar el archivo: los
// tres cuelgan de la escena, no de esta capa, porque FLAlertLayer::show se anade
// ahi.
bool SheepQueueLayer::hasPopupOpen() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;

    auto kids = scene->getChildren();
    if (!kids) return false;

    for (unsigned int i = 0; i < kids->count(); ++i) {
        if (geode::cast::typeinfo_cast<FLAlertLayer*>(kids->objectAtIndex(i))) return true;
    }
    return false;
}

// Clic derecho: brinco del jugador 2.
void SheepQueueLayer::rightKeyDown() {
    // Via propia de RobTop (CCMouseDelegate). Los botones del raton no llegan por el
    // teclado: el enumKeyCodes de este fork empieza en KEY_Backspace y no tiene
    // KEY_LButton ni KEY_RButton.
    bounce(true);
}

// Clic izquierdo en una zona vacia: brinco del jugador 1.
void SheepQueueLayer::ccTouchesBegan(cocos2d::CCSet*, cocos2d::CCEvent*) {
    // Modo estandar (el de por defecto de CCLayer) a proposito: un delegado estandar
    // nunca reclama el toque, asi que por construccion no puede quitarle nada a los
    // botones ni al arrastre del ScrollLayer. Y solo llega aqui lo que ningun
    // delegado con prioridad se haya quedado antes, es decir, los clics en zona
    // vacia.
    bounce(false);
}