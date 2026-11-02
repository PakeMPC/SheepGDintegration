#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/general.hpp>
#include "SheepI18n.hpp"
#include <vector>
#include <functional>
#include <fstream>
#include <optional>
#include <random>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// El modelo de queue_data.json, el archivo que comparten el mod y SheepGDRequest.
//
// - LevelModel: un nivel pedido, tal como viene en el JSON.
// - QueueData: el archivo entero (nivel actual, historial y cola), con su lectura,
//   su escritura, las operaciones que mueven la cola y la busqueda del archivo en
//   el disco.
//
// El formato lo define SheepGDRequest (C#). Las claves van en PascalCase porque
// C# serializa con System.Text.Json sin politica de nombres: el nombre de cada
// clave es el de la propiedad tal cual. Cambiar el formato obliga a tocar los dos
// programas a la vez.
// ---------------------------------------------------------------------------

// Modelo unico para todos los niveles: el actual, los del historial y los de la
// cola.
//
// Los campos con nombre son una vista de solo lectura: el mod nunca los modifica.
// Lo que se escribe de vuelta al archivo es 'raw', el JSON original de cada nivel
// (ver QueueData::saveToFile).
struct LevelModel {
    // Identidad de la peticion y del nivel.
    std::string requestId;           // GUID de la peticion; dos peticiones del mismo
                                     // nivel tienen requestId distinto
    std::string levelId;             // ID del nivel en GD, como texto
    std::string discordMessageId;    // sin uso en el mod (lo usa SheepGDRequest)
    std::string name;
    std::string author;

    // Datos del nivel que se muestran en la tarjeta y en la lista.
    std::string difficultyName;
    int stars = 0;
    std::string length;
    std::string customSongId;
    // Descargas y likes llegan ya compactados desde C# ("79.7M", "648.6K"), con
    // punto decimal. El signo de los likes va en isDislike; el numero, sin signo.
    std::string downloads;
    std::string likes;
    bool isDislike = false;
    int orbs = 0;
    int coins = 0;
    bool hasCoin1 = false;
    bool hasCoin2 = false;
    bool hasCoin3 = false;

    // Listas comunitarias (GDDL y AREDL). Vacio significa que el nivel no esta en
    // esa lista, y entonces no se dibuja la insignia.
    std::string gddlTier;
    std::string gddlTierColor;
    std::string aredlRank;           // llega con el '#' ya puesto
    bool hasAredl = false;

    // Recursos visuales. Las rutas *Path son de Avalonia (avares://): el mod no
    // carga esas imagenes, solo mira el nombre del archivo para elegir el sprite
    // equivalente del juego (sheep::featureState y sheep::platformIcon).
    std::string difficultyFacePath;  // sin uso en el mod
    std::string featureGlowPath;
    bool hasFeatureGlow = false;
    std::string thumbnailUrl;        // URL de la miniatura (ver sheep::thumbUrl)

    // Quien lo pidio y desde que plataforma.
    std::string requesterName;
    std::string platformIconPath;

    // Estado global que C# inyecta en cada nivel para sus widgets. El mod no los
    // usa: el reloj lo calcula a partir de autoSkipEndsAt.
    std::string timeRemainingText;

    // Instante absoluto del auto-skip, en segundos Unix UTC. 0 = sin
    // temporizador. Lo escribe SheepGDRequest una vez por nivel, y cada programa
    // calcula "fin - ahora" por su cuenta: los dos comparten el reloj del sistema.
    // Es un instante y no unos segundos restantes para no tener que reescribir el
    // JSON cada segundo.
    int64_t autoSkipEndsAt = 0;
    bool isRequestsEnabled = false;  // sin uso en el mod (ver timeRemainingText)

    // JSON original tal cual vino de C#. Se guarda para poder reescribir el archivo
    // sin perder campos que existan del lado de SheepGDRequest y que el mod no
    // conozca.
    matjson::Value raw;

    // Indica si es un nivel jugable. El nivel centinela en reposo se llama
    // "WAITING..." (sin distinguir mayusculas), y un nombre vacio es un nivel que
    // no se pudo leer.
    bool isValid() const {
        if (name.empty()) return false;
        auto upper = name;
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (upper.rfind("WAITING", 0) == 0) return false;
        return true;
    }

    // ID del nivel como numero; 0 si no es un numero valido.
    int levelIdAsInt() const {
        return geode::utils::numFromString<int>(levelId).unwrapOr(0);
    }

    // ID de la cancion como numero. 0 = cancion oficial del juego o NONG local: no
    // hay nada que descargar.
    int customSongIdAsInt() const {
        return geode::utils::numFromString<int>(customSongId).unwrapOr(0);
    }

    // Construye un LevelModel a partir de un objeto del JSON. Una clave que falte o
    // tenga otro tipo deja el valor por defecto, en vez de fallar: un JSON de una
    // version mas nueva o mas vieja de SheepGDRequest se sigue pudiendo leer.
    static LevelModel fromJson(const matjson::Value& obj) {
        LevelModel model;
        if (!obj.isObject()) return model;

        model.raw = obj;

        model.requestId          = obj["RequestId"].asString().unwrapOr("");
        model.levelId            = obj["LevelId"].asString().unwrapOr("");
        model.discordMessageId   = obj["DiscordMessageId"].asString().unwrapOr("");
        model.name               = obj["Name"].asString().unwrapOr("");
        model.author             = obj["Author"].asString().unwrapOr("");
        model.difficultyName     = obj["DifficultyName"].asString().unwrapOr("");
        model.stars              = (int)obj["Stars"].asInt().unwrapOr(0);
        model.length             = obj["Length"].asString().unwrapOr("");
        model.customSongId       = obj["CustomSongId"].asString().unwrapOr("");
        model.downloads          = obj["Downloads"].asString().unwrapOr("");
        model.likes              = obj["Likes"].asString().unwrapOr("");
        model.isDislike          = obj["IsDislike"].asBool().unwrapOr(false);
        model.orbs               = (int)obj["Orbs"].asInt().unwrapOr(0);
        model.coins              = (int)obj["Coins"].asInt().unwrapOr(0);
        model.hasCoin1           = obj["HasCoin1"].asBool().unwrapOr(false);
        model.hasCoin2           = obj["HasCoin2"].asBool().unwrapOr(false);
        model.hasCoin3           = obj["HasCoin3"].asBool().unwrapOr(false);
        model.gddlTier           = obj["GddlTier"].asString().unwrapOr("");
        model.gddlTierColor      = obj["GddlTierColor"].asString().unwrapOr("");
        model.aredlRank          = obj["AredlRank"].asString().unwrapOr("");
        model.hasAredl           = obj["HasAredl"].asBool().unwrapOr(false);
        model.difficultyFacePath = obj["DifficultyFacePath"].asString().unwrapOr("");
        model.featureGlowPath    = obj["FeatureGlowPath"].asString().unwrapOr("");
        model.hasFeatureGlow     = obj["HasFeatureGlow"].asBool().unwrapOr(false);
        model.thumbnailUrl       = obj["ThumbnailUrl"].asString().unwrapOr("");
        model.requesterName      = obj["RequesterName"].asString().unwrapOr("");
        model.platformIconPath   = obj["PlatformIconPath"].asString().unwrapOr("");
        model.timeRemainingText  = obj["TimeRemainingText"].asString().unwrapOr("");
        model.autoSkipEndsAt     = (int64_t)obj["AutoSkipEndsAtUnix"].asInt().unwrapOr(0);
        model.isRequestsEnabled  = obj["IsRequestsEnabled"].asBool().unwrapOr(false);

        return model;
    }
};

// El archivo queue_data.json completo.
//
// En memoria vive una sola copia, la de SheepData (SheepData.hpp), que lo relee
// cuando cambia en el disco y lo escribe con saveToFile cuando el mod mueve la
// cola.
struct QueueData {
    LevelModel current;               // "CurrentLevel": el nivel que se esta jugando
    std::vector<LevelModel> history;  // "History": los ya jugados, el ultimo al final
    std::vector<LevelModel> queue;    // "Queue": los pendientes, el siguiente primero

    // Raiz original del JSON, para conservar cualquier clave extra al reescribirlo.
    matjson::Value root;

    // -----------------------------------------------------------------------
    // Lectura
    // -----------------------------------------------------------------------

    // Lee el archivo. Distingue dos casos que no son lo mismo:
    //   - el archivo no existe: devuelve un QueueData vacio, que es el estado real
    //     (todavia no hay cola);
    //   - el archivo existe pero no se pudo leer (bloqueado, o un JSON roto o a
    //     medias): devuelve nullopt y deja el motivo en 'error', si se pasa.
    // Quien llama tiene que tratar el segundo como "no se sabe", no como "cola
    // vacia": dentro de una partida, una cola vacia se lee como fin de cola.
    //
    // No escribe en el log: quien llama reintenta en cada tick, y decide cuando
    // avisar para no repetir el mismo aviso dos veces por segundo.
    static std::optional<QueueData> loadFromFile(const std::filesystem::path& path,
                                                 std::string* error = nullptr) {
        QueueData data;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            // Con ec puesto no se sabe si existe: se trata como fallo de lectura.
            if (ec) {
                if (error) *error = ec.message();
                return std::nullopt;
            }
            return data;
        }

        std::ifstream file(path);
        auto result = matjson::parse(file);
        if (result.isErr()) {
            if (error) *error = fmt::format("{}", result.unwrapErr());
            return std::nullopt;
        }

        auto json = result.unwrap();
        data.root = json;

        if (json.contains("CurrentLevel") && json["CurrentLevel"].isObject()) {
            data.current = LevelModel::fromJson(json["CurrentLevel"]);
        }
        if (json.contains("History") && json["History"].isArray()) {
            for (auto& item : json["History"].asArray().unwrap()) {
                data.history.push_back(LevelModel::fromJson(item));
            }
        }
        if (json.contains("Queue") && json["Queue"].isArray()) {
            for (auto& item : json["Queue"].asArray().unwrap()) {
                data.queue.push_back(LevelModel::fromJson(item));
            }
        }

        return data;
    }

    // -----------------------------------------------------------------------
    // Escritura. El FileSystemWatcher de SheepGDRequest detecta el cambio, relee
    // el archivo y avisa a sus widgets, asi que la comunicacion funciona en los dos
    // sentidos sin nada mas.
    // -----------------------------------------------------------------------

    // Escribe la cola en el archivo. Devuelve false si no se pudo.
    //
    // El archivo se reconstruye a partir de 'raw' (el de cada nivel y el de la
    // raiz), no de los campos con nombre: asi se conservan las claves que el mod no
    // conoce. Un nivel sin 'raw' no se escribe.
    bool saveToFile(const std::filesystem::path& path) const {
        matjson::Value out = root.isObject() ? root : matjson::Value();

        out["CurrentLevel"] = current.raw.isObject() ? current.raw : matjson::Value();

        std::vector<matjson::Value> hist;
        hist.reserve(history.size());
        for (auto const& lv : history) if (lv.raw.isObject()) hist.push_back(lv.raw);
        out["History"] = matjson::Value(hist);

        std::vector<matjson::Value> q;
        q.reserve(queue.size());
        for (auto const& lv : queue) if (lv.raw.isObject()) q.push_back(lv.raw);
        out["Queue"] = matjson::Value(q);

        // JSON con sangria de 4 espacios. En la version de matjson que usa el SDK,
        // dump() devuelve el texto directamente.
        std::string text = out.dump(4);

        // Reemplazo atomico: se escribe un .tmp y se renombra encima del archivo.
        // Con dos procesos sin coordinacion, asi SheepGDRequest nunca puede leer un
        // JSON a medio escribir. El .tmp no dispara el watcher de C#, que solo
        // filtra *.json. No quitar el reemplazo atomico.
        auto tmp = path;
        tmp += ".tmp";

        {
            std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                log::error("[SheepGD] {}", sheep::trf("Log_CantOpenWrite", tmp.string()));
                return false;
            }
            file << text;

            // Se comprueba que la escritura del .tmp termino bien (con el disco lleno
            // se corta a medias). Si no, el renombrado pondria un JSON truncado encima
            // del bueno, que es justo lo que el reemplazo atomico existe para evitar.
            file.flush();
            if (!file.good()) {
                file.close();
                std::error_code rmEc;
                std::filesystem::remove(tmp, rmEc);
                log::error("[SheepGD] {}", sheep::trf("Log_CantWriteFile", tmp.string()));
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            // Si el renombrado falla (en Windows puede pasar si otro proceso tiene el
            // archivo abierto), se reintenta copiando encima. La copia ya no es
            // atomica, pero es mejor que perder el cambio.
            std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec);

            // Con su propio error_code: sin el, remove lanza una excepcion si no puede
            // borrar, y saldria del callback de un boton o de una tecla. Y uno aparte
            // para no pisar el resultado de la copia, que es el que decide.
            std::error_code rmEc;
            std::filesystem::remove(tmp, rmEc);
            if (ec) {
                log::error("[SheepGD] {}", sheep::trf("Log_CantReplace", ec.message()));
                return false;
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Mutaciones. Solo cambian la copia en memoria; quien las llama guarda despues
    // con SheepData::save().
    // -----------------------------------------------------------------------

    // Avanza: el actual pasa al historial y el primero de la cola pasa a ser el
    // actual. Sin cola, el actual pasa a ser el centinela WAITING.
    void skipToNext() {
        if (current.isValid()) history.push_back(current);
        if (!queue.empty()) {
            current = queue.front();
            queue.erase(queue.begin());
        } else {
            current = makeWaitingLevel();
        }
    }

    // Retrocede: vuelve el ultimo del historial, y el actual regresa al frente de
    // la cola.
    void backToPrevious() {
        if (history.empty()) return;
        if (current.isValid()) queue.insert(queue.begin(), current);
        current = history.back();
        history.pop_back();
    }

    // Descarta el actual sin mandarlo al historial y avanza.
    void discardCurrent() {
        if (!queue.empty()) {
            current = queue.front();
            queue.erase(queue.begin());
        } else {
            current = makeWaitingLevel();
        }
    }

    // Quita de la cola la peticion con ese requestId. Se busca por requestId y no
    // por nivel, porque el mismo nivel puede estar pedido dos veces.
    void removeFromQueue(std::string const& requestId) {
        if (requestId.empty()) return;
        queue.erase(
            std::remove_if(queue.begin(), queue.end(),
                [&](LevelModel const& lv) { return lv.requestId == requestId; }),
            queue.end()
        );
    }

    // Elige un nivel al azar de la cola y lo pone como actual; el que habia pasa al
    // historial.
    void pickRandom() {
        if (queue.empty()) return;

        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist(0, queue.size() - 1);
        auto idx = dist(rng);

        if (current.isValid()) history.push_back(current);
        current = queue[idx];
        queue.erase(queue.begin() + (long)idx);
    }

    // Salta directamente a un nivel de la cola: pasa a ser el actual y el que habia
    // se va al historial. Es pickRandom con el indice elegido a mano.
    void promoteFromQueue(size_t index) {
        if (index >= queue.size()) return;
        if (current.isValid()) history.push_back(current);
        current = queue[index];
        queue.erase(queue.begin() + (long)index);
    }

    // Fabrica el nivel centinela "WAITING...", que significa "no hay nivel actual".
    //
    // El nombre no se traduce, y no es un olvido: es parte del formato del archivo.
    // isValid() lo reconoce comparando el prefijo "WAITING", y SheepGDRequest
    // escribe exactamente esa palabra. Traducirlo romperia la deteccion en los dos
    // programas a la vez.
    //
    // Nota: se construye copiando el 'raw' del nivel actual y cambiando solo su
    // nombre, asi que en el archivo conserva el RequestId del nivel anterior.
    LevelModel makeWaitingLevel() const {
        LevelModel lv;
        lv.name = "WAITING...";
        lv.raw  = current.raw.isObject() ? current.raw : matjson::Value();
        if (lv.raw.isObject()) lv.raw["Name"] = "WAITING...";
        return lv;
    }

    // -----------------------------------------------------------------------
    // Donde esta queue_data.json. Hay dos fuentes: la ruta que el usuario eligio a
    // mano (guardada en los ajustes del mod) y la deteccion automatica. Ver
    // resolvePath para el orden.
    // -----------------------------------------------------------------------
        static constexpr auto SAVE_KEY = "queue-data-path";

    // La ruta que el usuario enlazo a mano. Vacia si nunca lo hizo.
    static std::filesystem::path getSavedPath() {
        return Mod::get()->getSavedValue<std::string>(SAVE_KEY, "");
    }

    // -----------------------------------------------------------------------
    // Deteccion automatica.
    //
    // SheepGDRequest guarda sus datos en:
    //   Windows  %LOCALAPPDATA%\SheepGDRequest
    //   Linux    $XDG_DATA_HOME, o ~/.local/share, + /SheepGDRequest
    //
    // Nota: el mod siempre es un binario de Windows. En Linux corre dentro de Wine
    // bajo Proton, asi que un #ifdef __linux__ no se cumple nunca (se compila la
    // rama de Windows igual) y %LOCALAPPDATA% apunta al prefijo de Wine, no al home
    // real. De ahi isWine() en tiempo de ejecucion, y llegar al home real por la
    // unidad Z:, que es donde Wine monta la raiz de Linux.
    //
    // Privacidad: bajo Wine esto enumera las carpetas de /home (y /var/home) del
    // sistema para encontrar el archivo, porque Wine no le pasa el home real a los
    // programas de Windows. Solo se comprueba si existe
    // .local/share/SheepGDRequest/queue_data.json dentro de cada una; no se lee
    // nada mas ni se envia nada a ningun sitio. El README y el about.md lo explican.
    //
    // Los logs de esta funcion salen siempre en ingles: el idioma se lee del
    // settings.json de SheepGDRequest, que esta en la misma carpeta que el archivo
    // que se esta buscando, asi que todavia no se conoce.
    // -----------------------------------------------------------------------
        static std::filesystem::path autoDetectPath() {
        namespace fs = std::filesystem;

        // Solo se guarda el acierto: si GD se abre antes que SheepGDRequest, basta
        // con volver a entrar a la escena para que lo intente otra vez.
        static fs::path cache;
        static bool yaSeLogueo = false;
        if (!cache.empty()) return cache;

        auto env = [](char const* k) { return geode::utils::getEnvironmentVariable(k); };

        // "/home/usuario" -> "Z:/home/usuario". std::filesystem acepta barras
        // normales en Windows, asi que no hay que convertirlas.
        auto zdrive = [](std::string const& unix) { return std::string("Z:") + unix; };

        std::vector<fs::path> candidatos;

        auto agregarDatos = [&](fs::path const& home) {
            candidatos.push_back(home / ".local" / "share" / "SheepGDRequest" / "queue_data.json");
        };

        if (geode::utils::platform::isWine()) {
            // 1. Las variables de entorno, por si alguna configuracion si las pasa.
            //    Comprobado que Proton no lo hace, pero no cuestan nada.
            if (auto x = env("XDG_DATA_HOME"); !x.empty())
                candidatos.push_back(fs::path(zdrive(x)) / "SheepGDRequest" / "queue_data.json");
            if (auto h = env("HOME"); !h.empty())
                agregarDatos(fs::path(zdrive(h)));

            // 2. El metodo que funciona: buscar. Z: es la raiz de Linux, asi que se
            //    enumeran los homes y se prueban uno a uno, sin depender de que Wine
            //    pase nada. /var/home es para las distribuciones atomicas
            //    (Silverblue, Bazzite).
            for (char const* raiz : { "Z:/home", "Z:/var/home" }) {
                std::error_code ec;
                fs::directory_iterator it(raiz, ec);
                if (ec) continue;
                for (auto const& e : it) {
                    std::error_code ec2;
                    if (e.is_directory(ec2) && !ec2) agregarDatos(e.path());
                }
            }
        }

        // 3. Windows nativo. Va la ultima y se prueba tambien bajo Wine, por si la
        //    version de Windows de SheepGDRequest estuviera en el mismo prefijo.
        if (auto l = env("LOCALAPPDATA"); !l.empty())
            candidatos.push_back(fs::path(l) / "SheepGDRequest" / "queue_data.json");

        for (auto const& c : candidatos) {
            std::error_code ec;
            bool hay = fs::exists(c, ec) && !ec;
            // Dos llaves en vez de una con "encontrado"/"no" dentro: esas palabras
            // sueltas no se traducen bien sin la frase que las rodea, y en otro
            // idioma pueden no ir al final.
            if (!yaSeLogueo)
                log::debug("[SheepGD] {}",
                           sheep::trf(hay ? "Log_AutoDetectHit" : "Log_AutoDetectMiss", c.string()));
            if (hay) { cache = c; break; }
        }

        if (!yaSeLogueo) {
            yaSeLogueo = true;
            log::info("[SheepGD] {}", sheep::trf("Log_AutoDetectResult",
                      geode::utils::platform::isWine(), candidatos.size(), cache.string()));
        }
        return cache;
    }

    // La ruta que se usa. Manda la guardada, pero solo si sigue existiendo: si
    // SheepGDRequest mueve su carpeta de datos, el enlace se recupera solo en vez de
    // obligar a elegir el archivo otra vez a mano.
    //
    // La ruta detectada no se guarda en SAVE_KEY: quedaria fija, y la siguiente
    // mudanza de la carpeta volveria a romper el enlace. Esa clave se reserva para lo
    // que el usuario eligio.
    static std::filesystem::path resolvePath() {
        auto saved = getSavedPath();
        if (!saved.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(saved, ec) && !ec) return saved;

            // El aviso, una sola vez: a resolvePath la llaman hasLinkedFile y
            // getLinkedFilePath, y las dos en cada visita a la escena.
            //
            // La clave no se borra, a proposito. Una ruta elegida a mano puede estar
            // en un disco que hoy no este montado; borrarla convertiria un "ahora no
            // esta" en perder la eleccion del usuario para siempre. Mientras tanto
            // manda la automatica, y si el disco vuelve, la manual recupera la
            // prioridad sola.
            static bool avisado = false;
            if (!avisado) {
                avisado = true;
                log::warn("[SheepGD] {}", sheep::trf("Log_SavedPathGone", saved.string()));
            }
        }
        return autoDetectPath();
    }

    static std::filesystem::path getLinkedFilePath() { return resolvePath(); }
    static bool hasLinkedFile()                      { return !resolvePath().empty(); }

    // Pide al usuario que enlace queue_data.json: primero un aviso con "Cancelar" y
    // "Buscar", y despues el selector de archivos del sistema. La ruta elegida se
    // guarda en SAVE_KEY.
    //
    // onLinked recibe la ruta si se eligio un archivo. onCancelled, si se pulso
    // "Cancelar", se cerro el selector sin elegir o el selector fallo.
    static void promptForFile(
        std::function<void(std::filesystem::path const&)> onLinked,
        std::function<void()> onCancelled = nullptr
    ) {
        // Los textos van en variables locales: createQuickPopup recibe char const*,
        // y el c_str() de un tr() suelto apuntaria a un temporal ya destruido.
        auto titulo = sheep::tr("Link_Title");
        auto cuerpo = sheep::tr("Link_Body");
        auto cancel = sheep::tr("Link_Cancel");
        auto buscar = sheep::tr("Link_Browse");

        geode::createQuickPopup(
            titulo.c_str(),
            cuerpo.c_str(),
            cancel.c_str(), buscar.c_str(),
            [onLinked, onCancelled](FLAlertLayer*, bool btn2) {
                if (!btn2) {
                    if (onCancelled) onCancelled();
                    return;
                }

                utils::file::FilePickOptions options;
                options.filters = { utils::file::FilePickOptions::Filter(sheep::tr("Link_JsonFilter"), {"*.json"}) };

                // El selector es asincrono: la respuesta llega en el callback.
                async::spawn(
                    utils::file::pick(utils::file::PickMode::OpenFile, options),
                    [onLinked, onCancelled](Result<std::optional<std::filesystem::path>> result) {
                        if (!result.isOk()) {
                            if (onCancelled) onCancelled();
                            return;
                        }
                        auto optPath = result.unwrap();
                        if (!optPath.has_value()) {
                            FLAlertLayer::create(sheep::tr("Link_NoneTitle").c_str(),
                                                 sheep::tr("Link_NoneBody").c_str(),
                                                 sheep::tr("Alert_Ok").c_str())->show();
                            if (onCancelled) onCancelled();
                            return;
                        }
                        auto path = optPath.value();
                        Mod::get()->setSavedValue(SAVE_KEY, path.string());
                        FLAlertLayer::create(sheep::tr("Link_OkTitle").c_str(),
                                             sheep::tr("Link_OkBody").c_str(),
                                             sheep::tr("Alert_Ok").c_str())->show();
                        onLinked(path);
                    }
                );
            }
        );
    }
};