#pragma once
#include <Geode/Geode.hpp>
#include "QueueData.hpp"
#include "SheepSongs.hpp"
#include "SheepPrefetcher.hpp"
#include "SheepTime.hpp"
#include "SheepI18n.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <string>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Dos cosas que usan casi todos los archivos del mod:
//
// - En el espacio de nombres sheep: los ajustes del mod, el estado que cruza de un
//   nivel a otro y la pregunta "se ha acabado la cola".
// - La clase SheepData: el servicio global que mantiene en memoria el contenido de
//   queue_data.json y avisa cuando cambia.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Ajustes del mod. Viven en el save.json que gestiona Geode (Mod::getSavedValue),
// asi que sobreviven al cierre del juego sin que el mod tenga que escribir nada.
//
// Las claves estan aqui, en un solo sitio, porque las usan codigos que no se
// conocen entre si: el popup de ajustes, que las escribe, y los hooks de la
// partida, que las leen. Una cadena literal repetida en dos archivos es una errata
// esperando a ocurrir.
// ---------------------------------------------------------------------------
namespace sheep {

    // Indica si se esta jugando el nivel placeholder. Vive fuera de la capa por la
    // misma razon que SheepQueueLayer::s_returnTicket: el PlayLayer no sobrevive al
    // salto de nivel, y el placeholder consiste precisamente en seguir esperando
    // entre partidas.
    inline bool& inPlaceholder() { static bool v = false; return v; }

    // Indica si el nivel que llega lo puso un salto de SheepLevelLauncher (y no el
    // jugador desde la escena). Lo usa el PlayLayer para corregir el cursor.
    //
    // Vive aqui y no en el espacio de nombres anonimo de main.cpp por una razon
    // concreta: ese espacio da enlace interno, asi que la bandera solo existiria
    // dentro de main.cpp y ningun extern desde otro archivo la encontraria. Una
    // funcion inline con un static dentro es una sola instancia para todo el mod;
    // es el mismo patron que inPlaceholder().
    inline bool& swappedByAutoSkip() { static bool v = false; return v; }

    // Reloj del auto-skip dentro de la partida: si se muestra, y si solo en los
    // ultimos 10 segundos.
    inline constexpr char const* kKeyIngameTimer = "timer-ingame";
    inline constexpr char const* kKeyLastTenOnly = "timer-last-10s";

    // El valor por defecto de cada ajuste vive solo en su funcion; el popup de
    // ajustes tambien lee por ellas, para no mostrar una cosa y hacer otra.
    inline bool ingameTimerEnabled() {
        return Mod::get()->getSavedValue<bool>(kKeyIngameTimer, true);
    }
    inline bool lastTenOnly() {
        return Mod::get()->getSavedValue<bool>(kKeyLastTenOnly, false);
    }

    // Cubo de Sheepy: si se usa, y si solo en las partidas lanzadas desde la cola.
    inline constexpr char const* kKeySheepyIcon    = "sheepy-icon";
    inline constexpr char const* kKeySheepyOnlyReq = "sheepy-only-requests";

    inline bool sheepyEnabled() {
        return Mod::get()->getSavedValue<bool>(kKeySheepyIcon, false);
    }
    inline bool sheepyOnlyRequests() {
        return Mod::get()->getSavedValue<bool>(kKeySheepyOnlyReq, true);
    }

    // Que hacer cuando se acaba el tiempo del ultimo nivel de la cola. Se guarda
    // como entero, no como varios bool, para poder anadir modos sin migrar la
    // clave.
    enum EndOfQueue {
        kEndStay        = 0,   // seguir en el nivel hasta que entre uno nuevo
        kEndExitToList  = 1,   // salir solo a la lista
        kEndUntilDeath  = 2,   // seguir hasta morir, y entonces salir
        kEndPlaceholder = 3,   // ir a un nivel placeholder
    };
    inline constexpr int kEndOfQueueCount = 4;

    inline constexpr char const* kKeyEndOfQueue    = "end-of-queue";
    inline constexpr char const* kKeyPlaceholderId = "placeholder-level";
    inline constexpr char const* kKeySwapOnDeath   = "swap-on-death";

    // Modo de fin de cola guardado, siempre dentro del rango valido.
    inline int endOfQueueMode() {
        int v = Mod::get()->getSavedValue<int>(kKeyEndOfQueue, kEndStay);
        // Se acota al leer y no al escribir: si un save.json antiguo trae el valor
        // de un modo que ya no existe, se usa el modo por defecto en vez de dejar
        // el mod en un estado sin definir.
        return (v < 0 || v >= kEndOfQueueCount) ? kEndStay : v;
    }

    // ID del nivel placeholder tal como lo escribio el usuario, y como numero (0 si
    // no es un numero valido).
    inline std::string placeholderLevelText() {
        return Mod::get()->getSavedValue<std::string>(kKeyPlaceholderId, "");
    }
    inline int placeholderLevelId() {
        return geode::utils::numFromString<int>(placeholderLevelText()).unwrapOr(0);
    }

    // "Swap on death": si el cambio de nivel espera a que el jugador muera en vez de
    // ser inmediato. Afecta tambien a los saltos normales, no solo al fin de cola.
    inline bool swapOnDeath() {
        return Mod::get()->getSavedValue<bool>(kKeySwapOnDeath, false);
    }

    // Nombre visible de un modo de fin de cola.
    //
    // Devuelve std::string y no char const*, y no es un detalle de estilo: el texto
    // sale de la tabla de traducciones en tiempo de ejecucion, asi que no hay
    // literales a los que apuntar. Quien lo pase a setString necesita un .c_str().
    inline std::string endOfQueueName(int mode) {
        switch (mode) {
            case kEndExitToList:  return tr("End_Exit");
            case kEndUntilDeath:  return tr("End_Death");
            case kEndPlaceholder: return tr("End_Placeholder");
            default:              return tr("End_Stay");
        }
    }

    // Indica si el JSON esta ahora mismo en estado de "fin de cola": no queda nada
    // en la cola y el nivel actual es el centinela WAITING o ya agoto su tiempo.
    //
    // Vive aqui, y no dentro del hook de PlayLayer donde nacio, porque la escena
    // necesita exactamente la misma pregunta para decidir si muestra el placeholder
    // en la tarjeta. Dos copias se desincronizarian el dia que se cambiara una.
    //
    // Es un estado y no una transicion: al vaciarse la cola el requestId no cambia,
    // asi que no se puede detectar comparando requestId como un nivel nuevo. Por
    // auto-skip, SheepGDRequest no escribe nada si no queda cola (el nivel actual
    // se queda con su plazo vencido), y con la tecla E el centinela se fabrica a
    // partir del nivel anterior, con su RequestId. Los dos programas comparten el
    // reloj del sistema, asi que "ahora >= fin" es fiable.
    inline bool queueEnded(QueueData const& d) {
        if (!d.queue.empty()) return false;

        auto const& cur = d.current;
        if (!cur.isValid()) return true;   // centinela WAITING
        return cur.autoSkipEndsAt > 0 && sheep::nowUnix() >= cur.autoSkipEndsAt;
    }

    // Indica si la tarjeta de la escena debe mostrar el nivel placeholder en vez
    // del actual: el modo es "placeholder", hay un ID valido y la cola se acabo.
    inline bool wantsPlaceholderCard(QueueData const& d) {
        return endOfQueueMode() == kEndPlaceholder
            && placeholderLevelId() > 0
            && queueEnded(d);
    }

} // namespace sheep

// ---------------------------------------------------------------------------
// Servicio global de datos: la copia en memoria de queue_data.json.
//
// Vive fuera del grafo de escenas: se registra en el scheduler del director, no en
// un nodo. Por eso sigue sondeando el archivo aunque el jugador este dentro de un
// PlayLayer, en el menu principal o en cualquier otro sitio. Es la razon de que
// exista: antes el sondeo era un schedule de SheepQueueLayer, que moria al entrar
// a un nivel y se llevaba los datos con el, asi que dentro de la partida nadie
// miraba el JSON.
//
// Ademas del sondeo, su tick empuja las descargas de audio y la precarga del
// siguiente nivel (onTick).
//
// Uso: SheepData::get()->data() para leer, mutar con las funciones de QueueData y
// despues save(); subscribe() para enterarse de los cambios.
// ---------------------------------------------------------------------------
class SheepData : public CCObject {
public:
    // Callback de suscripcion: recibe los datos ya actualizados.
    using Listener = std::function<void(QueueData const&)>;

    // La instancia unica. Se crea la primera vez que se pide.
    static SheepData* get() {
        static SheepData* inst = [] {
            auto p = new SheepData();
            p->retain();          // nunca se libera: dura lo que dura el juego
            return p;
        }();
        return inst;
    }

    // -- Archivo ------------------------------------------------------------

    // Indica si ya hay un archivo asignado. Hasta que la escena asigna uno, el
    // servicio no sondea nada.
    bool hasPath() const { return !m_path.empty(); }
    std::filesystem::path const& path() const { return m_path; }

    // Asigna el archivo, arranca el sondeo y lo lee en el acto, avisando a los
    // suscriptores.
    void setPath(std::filesystem::path const& p) {
        m_path      = p;
        m_lastWrite = {};        // fuerza relectura
        start();
        poll(true);
    }

    // Arranca el sondeo. Es idempotente: llamarlo dos veces no lo duplica.
    void start() {
        if (m_running || m_path.empty()) return;
        m_running = true;
        CCDirector::get()->getScheduler()->scheduleSelector(
            schedule_selector(SheepData::onTick), this, kPollInterval, false);
    }

    // -- Datos --------------------------------------------------------------

    // La copia en memoria. La version no constante se usa para mutar la cola antes
    // de llamar a save().
    QueueData&       data()       { return m_data; }
    QueueData const& data() const { return m_data; }

    // Escribe el JSON y avisa a los suscriptores sin esperar al sondeo. Apunta la
    // fecha de escritura resultante para que el siguiente sondeo no relea el
    // archivo que el propio mod acaba de escribir.
    //
    // Si no se puede escribir, deshace la mutacion hecha en data(): vuelve a la
    // ultima copia que coincidia con el archivo y avisa con ella. Sin eso, el cambio
    // se quedaria solo en memoria; el vigia de la partida lanzaria un nivel que el
    // JSON (y con el SheepGDRequest) no tiene, hasta que C# lo pisara a mitad de
    // partida. Bajo Wine la escritura puede fallar si C# tiene el archivo abierto.
    bool save() {
        if (m_path.empty() || !m_data.saveToFile(m_path)) {
            log::warn("[SheepGD] {}", sheep::tr("Log_UndoUnsaved"));
            m_data = m_lastGood;
            notify();
            return false;
        }

        std::error_code ec;
        m_lastWrite = std::filesystem::last_write_time(m_path, ec);
        m_lastGood  = m_data;
        notify();
        return true;
    }

    // -- Suscripcion --------------------------------------------------------
    //
    // subscribe devuelve un id para darse de baja con unsubscribe. Darse de baja en
    // el destructor del suscriptor es obligatorio: las escenas se destruyen, y
    // llamar a un callback que captura un puntero ya liberado es un cierre del juego
    // dificil de rastrear.
    int subscribe(Listener cb) {
        int id = ++m_nextId;
        m_listeners[id] = std::move(cb);
        return id;
    }

    void unsubscribe(int id) { m_listeners.erase(id); }

private:
    static constexpr float kPollInterval = 0.5f;   // segundos entre ticks

    SheepData() = default;

    // Tick del servicio, cada kPollInterval segundos.
    void onTick(float) {
        // El reloj de todos los enfriamientos de audio (SheepSongs.hpp). Va aqui y
        // no dentro de prefetchSongs porque a esa se la llama dos veces en el mismo
        // tick cuando el JSON cambia (poll tambien la llama). Este es el unico sitio
        // que se ejecuta exactamente una vez por tick, pase lo que pase.
        ++sheep::detail::tickCount();

        // Cupo de consultas al disco de este tick. Bajo Wine, preguntar si existe un
        // archivo cuesta del orden de medio milisegundo, y sin cupo la primera pasada
        // por los SFX de un nivel congelaba el juego. Se repone aqui por la misma
        // razon de arriba: es el unico punto que corre una vez por tick.
        sheep::detail::diskBudget() = sheep::kDiskChecksPerTick;

        poll(false);

        // Fuera de poll a proposito: esto no reacciona a que el archivo cambie, sino
        // que empuja las descargas pendientes fase a fase en cada tick.
        sheep::prefetchSongs(m_data);

        // Y el nivel entero del actual y del siguiente, que es de donde salen los
        // SFX y las canciones multiples: el JSON no puede aportarlos.
        SheepPrefetcher::get()->update(m_data);
    }

    // Relee el archivo si su fecha de escritura cambio (o siempre, con force) y
    // avisa a los suscriptores.
    void poll(bool force) {
        if (m_path.empty()) return;

        std::error_code ec;
        auto write = std::filesystem::last_write_time(m_path, ec);
        if (ec) return;
        if (!force && write == m_lastWrite) return;

        // Si la lectura falla (archivo bloqueado, o un JSON roto o a medias), se
        // conservan los datos que habia y no se apunta la fecha, asi que el siguiente
        // tick lo reintenta. Tomar el fallo como cola vacia sacaria al jugador del
        // nivel como si se hubiera acabado la cola. El aviso sale una vez por cada
        // fecha de escritura, no en cada reintento.
        std::string error;
        auto loaded = QueueData::loadFromFile(m_path, &error);
        if (!loaded) {
            if (write != m_lastFailedWrite) {
                log::warn("[SheepGD] {}", sheep::trf("Log_BadJson", error));
                m_lastFailedWrite = write;
            }
            return;
        }
        m_lastWrite = write;
        m_data      = std::move(*loaded);
        m_lastGood  = m_data;

        // Antes de avisar a nadie: la cola cambio, asi que puede haber canciones
        // nuevas que pedir. Va aqui y no en un suscriptor porque tiene que ocurrir
        // tambien cuando no hay ninguna escena viva, es decir, durante la partida.
        sheep::prefetchSongs(m_data);

        notify();
    }

    // Llama a todos los suscriptores con los datos actuales.
    void notify() {
        // Se recorre una copia a proposito: un suscriptor puede darse de baja desde
        // su propio callback, y eso invalidaria el iterador del mapa original.
        auto snapshot = m_listeners;
        for (auto const& [id, cb] : snapshot) if (cb) cb(m_data);
    }

    std::filesystem::path           m_path;         // queue_data.json; vacia hasta setPath
    std::filesystem::file_time_type m_lastWrite{};  // fecha de la ultima lectura o escritura
    std::filesystem::file_time_type m_lastFailedWrite{};  // la del ultimo fallo ya avisado
    QueueData                       m_data;
    QueueData                       m_lastGood;     // ultima copia igual al archivo (save)

    std::map<int, Listener> m_listeners;   // suscriptores, por id
    int  m_nextId  = 0;
    bool m_running = false;                // el tick esta programado
};