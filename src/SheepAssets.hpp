#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <string_view>
#include <vector>
#include "SheepI18n.hpp"

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Traduccion de los datos de SheepGDRequest a recursos visuales del juego.
//
// El JSON describe cada nivel con textos y rutas pensados para la interfaz de
// SheepGDRequest (Avalonia): "Medium Demon", "avares://.../discord.png". Aqui se
// convierten en lo que dibuja GD: caras de dificultad nativas, iconos de
// estadisticas, sprites propios del mod y colores.
//
// Solo cabecera: todas las funciones son inline y no hace falta un .cpp aparte.
// ---------------------------------------------------------------------------
namespace sheep {

// ---------------------------------------------------------------------------
// Sprites seguros
// ---------------------------------------------------------------------------

    // Indica si existe un frame con ese nombre en la cache de frames del juego.
    //
    // Nota: Geode no devuelve nullptr cuando un frame no existe, sino un
    // CCSpriteFrame que apunta a una textura de reserva (el cuadro rosa). Para
    // detectarlo se pide una sola vez un nombre imposible, se guarda esa textura y
    // despues se compara con ella. Sin esto, frameExists diria siempre true y
    // cualquier cadena de alternativas no tendria efecto.
    inline bool frameExists(std::string const& name) {
        auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();

        // Se retiene: si el juego vaciara la cache de texturas, otra textura podria
        // nacer en la misma direccion y pasar por la de reserva.
        static CCTexture2D* fallbackTex = [cache] {
            auto f = cache->spriteFrameByName("__sheepgd_frame_inexistente__.png");
            auto t = f ? f->getTexture() : nullptr;
            if (t) t->retain();
            return t;
        }();

        auto frame = cache->spriteFrameByName(name.c_str());
        if (!frame) return false;
        if (fallbackTex && frame->getTexture() == fallbackTex) return false;
        return true;
    }

// Prueba una lista de nombres de frame y devuelve un sprite del primero que exista.
// Si no existe ninguno, devuelve nullptr (nunca un sprite de reserva) y lo avisa
// en el log con la lista de nombres probados.
    inline CCSprite* frameSpriteAny(std::vector<std::string> const& candidates) {
    for (auto const& name : candidates) {
        if (frameExists(name)) {
            if (auto spr = CCSprite::createWithSpriteFrameName(name.c_str())) return spr;
        }
    }
    std::string tried;
    for (auto const& c : candidates) tried += c + " ";
        log::warn("[SheepGD] {}", trf("Log_NoSpriteFrame", tried));
    return nullptr;
}

// frameSpriteAny con un solo nombre.
inline CCSprite* frameSprite(std::string const& name) {
    return frameSpriteAny(std::vector<std::string>{name});
}

    namespace detail {
        // CCSprite::create de un recurso propio, con nullptr si no existe, sin log.
        //
        // No basta con mirar si CCSprite::create da nullptr: con un recurso del mod
        // que no existe puede devolver el cuadro rosa de reserva. Es el mismo truco
        // que frameExists: se crea una vez un nombre imposible, se guarda su
        // textura (retenida, por la misma razon) y se compara con ella. Si en
        // cambio da nullptr, la textura guardada es nullptr y solo cuenta el
        // nullptr. Asi funciona sea cual sea el comportamiento real.
        inline CCSprite* createOwnQuiet(char const* name) {
            static CCTexture2D* fallbackTex = [] {
                auto probe = CCSprite::create("__sheepgd_sprite_inexistente__.png"_spr);
                auto t = probe ? probe->getTexture() : nullptr;
                if (t) t->retain();
                return t;
            }();

            auto s = CCSprite::create(name);
            if (!s) return nullptr;
            if (fallbackTex && s->getTexture() == fallbackTex) return nullptr;
            return s;
        }
    }

    // Sprite propio del mod ("x.png"_spr), o nullptr si el recurso no existe, que
    // ademas se avisa en el log. Para los recursos propios se usa esta y no
    // CCSprite::create, que con un recurso que falta puede dar el cuadro rosa.
    inline CCSprite* createOwn(char const* name) {
        auto s = detail::createOwnQuiet(name);
        if (!s) log::warn("[SheepGD] {}", trf("Log_MissingOwnAsset", name));
        return s;
    }

    // Indica si existe un recurso propio, sin avisar en el log.
    inline bool ownSpriteExists(char const* name) {
        return detail::createOwnQuiet(name) != nullptr;
    }

// Sprite de la carpeta resources/ del mod, por nombre de archivo ("gddl.png"). Es
// el equivalente de "gddl.png"_spr para un nombre que se decide en ejecucion.
// nullptr si no existe.
inline CCSprite* modSprite(std::string const& fileName) {
    auto full = fmt::format("{}/{}", Mod::get()->getID(), fileName);
    return createOwn(full.c_str());
}

    // Sprite propio del mod escalado a una altura concreta, en puntos. Con
    // targetHeight = 0 se deja a su tamano.
    //
    // Se trabaja con la altura y no con un factor de escala porque el factor
    // depende del tamano del PNG: si el recurso se reexporta al doble de
    // resolucion, con la altura el boton mide lo mismo y con la escala se duplica.
    inline CCSprite* ownSprite(char const* sprName, float targetHeight = 0.f) {
        auto s = createOwn(sprName);   // ya avisa en el log si falta
        if (!s) return nullptr;
        if (targetHeight > 0.f) {
            s->setScale(targetHeight / std::max(1.f, s->getContentSize().height));
        }
        return s;
    }

    // Boton de texto sobre el fondo propio del mod (GJ_button_01.png de resources/).
    //
    // CCScale9Sprite estira los bordes sin deformar las esquinas redondeadas, asi
    // que el boton crece con el texto y sigue viendose bien. Es lo mismo que hace
    // ButtonSprite por dentro, pero ButtonSprite usa el GJ_button_01 del juego, y
    // ese lo cambian los texture packs. Si falta el recurso propio, se usa
    // ButtonSprite como alternativa.
    //
    // Devuelve el nodo del boton, listo para meterlo en un CCMenuItemSpriteExtra.
    inline CCNode* ownTextButton(std::string const& text, float height,
                                 float labelScale = 0.5f,
                                 float padding    = 14.f,
                                 char const* font = "goldFont.fnt") {
        auto label = CCLabelBMFont::create(text.c_str(), font);
        label->setScale(labelScale);

        // Se pregunta antes si existe: CCScale9Sprite no avisaria de un recurso que
        // falta, y saldria el cuadro rosa estirado.
        cocos2d::extension::CCScale9Sprite* bg = nullptr;
        if (ownSpriteExists("GJ_button_01.png"_spr))
            bg = cocos2d::extension::CCScale9Sprite::create("GJ_button_01.png"_spr);
        if (!bg) {
            log::warn("[SheepGD] {}", tr("Log_MissingButtonAsset"));
            return ButtonSprite::create(text.c_str(), font, "GJ_button_01.png", labelScale);
        }

        const float w = label->getScaledContentSize().width + padding * 2.f;
        bg->setContentSize({w, height});
        // El centro visual de GJ_button_01.png no es su centro geometrico: el borde
        // de abajo es mas grueso, y un texto centrado sin mas se ve caido. Este
        // ajuste lo compensa, y como depende del recurso vale para todos los
        // botones que lo usen.
        constexpr float kLabelNudgeY = 2.f;
        label->setPosition({w / 2.f, height / 2.f + kLabelNudgeY});
        bg->addChild(label, 1);
        return bg;
    }
// ---------------------------------------------------------------------------
// Utilidades de texto
// ---------------------------------------------------------------------------

// Nombre del archivo de una ruta, sin extension y en minusculas.
// "avares://SheepGDRequest/Assets/.../demon-medium.png" -> "demon-medium"
inline std::string pathStem(std::string_view path) {
    if (path.empty()) return "";
    auto slash = path.find_last_of("/\\");
    auto file  = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    auto dot   = file.find_last_of('.');
    auto stem  = (dot == std::string_view::npos) ? file : file.substr(0, dot);

    std::string out(stem);
    for (auto& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

// Clave de comparacion: solo letras y numeros, en mayusculas.
// "Medium Demon" -> "MEDIUMDEMON"
inline std::string normalizeKey(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (auto c : text) {
        if (std::isalnum((unsigned char)c)) out += (char)std::toupper((unsigned char)c);
    }
    return out;
}

// Indica si 'needle' aparece dentro de 'haystack'.
inline bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

// "#FF00AA" / "FF00AA" -> ccColor3B. Si no se puede leer, devuelve 'fallback'.
// Con mas de seis cifras (un color con alfa, "#AARRGGBB" como los de Avalonia) se
// toman las seis ultimas.
inline ccColor3B parseHexColor(std::string_view hex, ccColor3B fallback = {255, 255, 255}) {
    std::string h;
    for (auto c : hex) if (std::isxdigit((unsigned char)c)) h += c;
    if (h.size() < 6) return fallback;
    h = h.substr(h.size() - 6); // si viene con alfa delante, se descarta
    auto val = std::strtoul(h.c_str(), nullptr, 16);
    return ccColor3B{
        (GLubyte)((val >> 16) & 0xFF),
        (GLubyte)((val >> 8) & 0xFF),
        (GLubyte)(val & 0xFF)
    };
}

// ---------------------------------------------------------------------------
// Dificultad
// ---------------------------------------------------------------------------

    // Indice del sprite de dificultad de GD a partir del nombre que manda
    // SheepGDRequest y, si el nombre no basta, de las estrellas.
    //
    // Los indices se comprobaron en el juego con un volcado de getDifficultyFrame
    // (una sonda que ya no esta en el codigo). El rango valido es 0..10:
    //   0 N/A · 1 Easy · 2 Normal · 3 Hard · 4 Harder · 5 Insane
    //   6 Demon/Hard Demon · 7 Easy Demon · 8 Medium Demon
    //   9 Insane Demon · 10 Extreme Demon
    // El 6 es el Demon original; en 2.1 se anadieron los otros cuatro subtipos, por
    // eso el Hard Demon no tiene indice propio y el orden parece desordenado.
    inline int difficultyIndex(std::string_view difficultyName, int stars) {
        auto key = normalizeKey(difficultyName);

        if (key.empty() || key == "NA" || key == "NONE" || key == "UNRATED") return 0;
        if (key == "AUTO")          return 0;   // sin cara propia en esta serie
        if (key == "EASY")          return 1;
        if (key == "NORMAL")        return 2;
        if (key == "HARD")          return 3;
        if (key == "HARDER")        return 4;
        if (key == "INSANE")        return 5;
        if (key == "HARDDEMON" || key == "DEMON" || key == "DEMONTODOS") return 6;
        if (key == "EASYDEMON")     return 7;
        if (key == "MEDIUMDEMON")   return 8;
        if (key == "INSANEDEMON")   return 9;
        if (key == "EXTREMEDEMON")  return 10;

        // Red de seguridad por si el texto llega con adornos o en otro idioma.
        if (contains(key, "DEMON")) {
            if (contains(key, "EXTREME")) return 10;
            if (contains(key, "INSANE"))  return 9;
            if (contains(key, "MEDIUM"))  return 8;
            if (contains(key, "EASY"))    return 7;
            return 6;
        }

        // Ultimo recurso: deducirlo de las estrellas, con el reparto de GD.
        switch (stars) {
            case 1:            return 0;   // auto
            case 2:            return 1;
            case 3:            return 2;
            case 4: case 5:    return 3;
            case 6: case 7:    return 4;
            case 8: case 9:    return 5;
            case 10:           return 6;
            default:           return 0;
        }
    }

    // Halo de rate (featured, epic, legendary, mythic) a partir de la ruta de la
    // imagen que manda SheepGDRequest. El halo lo dibuja el propio
    // GJDifficultySprite; aqui solo se traduce el nombre del archivo al enum del
    // juego.
    inline GJFeatureState featureState(std::string_view featureGlowPath, bool hasGlow) {
    if (!hasGlow) return GJFeatureState::None;
    auto stem = pathStem(featureGlowPath);

    if (contains(stem, "mythic")) return GJFeatureState::Mythic;
    if (contains(stem, "legend")) return GJFeatureState::Legendary;
    if (contains(stem, "epic"))   return GJFeatureState::Epic;
    return GJFeatureState::Featured;
}

    // Crea la cara de dificultad nativa de GD comprobando antes que su frame
    // exista. Si no existiera, cocos pintaria el cuadro rosa de reserva a tamano
    // nativo, que es enorme y se sale de la tarjeta.
    //
    // Orden de alternativas: el indice pedido, despues el demon generico (6) o el
    // N/A (0) segun corresponda, y por ultimo el 0; cada uno en version larga y
    // corta. Devuelve nullptr si no existe ninguno.
    inline GJDifficultySprite* difficultyFaceNative(int idx) {
        idx = std::clamp(idx, 0, 10);
        const int fallback = (idx >= 6) ? 6 : 0;

        for (int candidate : { idx, fallback, 0 }) {
            for (auto variant : { GJDifficultyName::Long, GJDifficultyName::Short }) {
                std::string frame = GJDifficultySprite::getDifficultyFrame(candidate, variant).c_str();

                if (frameExists(frame)) {
                    if (candidate != idx)
                        log::warn("[SheepGD] {}", trf("Log_DiffIdxFallback", idx, candidate, frame));
                    return GJDifficultySprite::create(candidate, variant);
                }

                log::warn("[SheepGD] {}", trf("Log_DiffFrameMissing", frame, candidate));
            }
        }
        return nullptr;
    }

    // Icono de la plataforma desde la que se pidio el nivel (Discord, Twitch...).
    //
    // El JSON manda una ruta avares de Avalonia; solo interesa el nombre del archivo
    // ("discord"). Como un nombre del mod inexistente devuelve el cuadro rosa en
    // vez de nullptr, se valida contra la lista de los que existen en
    // resources/platform/ y cualquier plataforma desconocida cae en global.png.
    inline CCSprite* platformIcon(std::string_view platformIconPath) {
    static const std::vector<std::string> known = {
        "discord", "twitch", "youtube", "tiktok", "kick", "steam", "facebook", "global"
    };

    auto stem = pathStem(platformIconPath);
    if (std::find(known.begin(), known.end(), stem) == known.end()) {
        if (!stem.empty()) log::debug("[SheepGD] {}", trf("Log_UnknownPlatform", stem));
        stem = "global";
    }
    return modSprite(stem + ".png");
}

// ---------------------------------------------------------------------------
// Iconos de estadisticas. Son los del propio juego (los de la pantalla de
// informacion del nivel), salvo GDDL y AREDL, que son recursos del mod.
// ---------------------------------------------------------------------------

// Estrellas, o lunas en un platformer (con la estrella si el frame no existe).
inline CCSprite* iconStars(bool platformer) {
    return platformer ? frameSpriteAny({"GJ_moonsIcon_001.png", "GJ_starsIcon_001.png"})
                      : frameSprite("GJ_starsIcon_001.png");
}
inline CCSprite* iconLength()            { return frameSprite("GJ_timeIcon_001.png"); }
inline CCSprite* iconDownloads()         { return frameSprite("GJ_downloadsIcon_001.png"); }
// Like o dislike, segun el signo de los likes.
inline CCSprite* iconLikes(bool dislike) {
    return dislike ? frameSpriteAny({"GJ_dislikesIcon_001.png", "GJ_likesIcon_001.png"})
                   : frameSprite("GJ_likesIcon_001.png");
}
inline CCSprite* iconOrbs()              { return frameSprite("currencyOrbIcon_001.png"); }
// Moneda verificada o sin verificar (con el icono de verificada si el otro no
// existe).
inline CCSprite* iconCoin(bool verified) {
    return verified ? frameSprite("GJ_coinsIcon_001.png")
                    : frameSpriteAny({"GJ_coinsIcon2_001.png", "GJ_coinsIcon_001.png"});
}
inline CCSprite* iconGddl()  { return modSprite("gddl.png"); }
inline CCSprite* iconAredl() { return modSprite("aredl.png"); }

// Indica si el nivel es un platformer. En un platformer la duracion llega como un
// texto que contiene "Plat" ("Platformer", por ejemplo), asi que basta con buscar
// "PLAT" en ella.
inline bool isPlatformer(std::string_view length) {
    auto key = normalizeKey(length);
    return contains(key, "PLAT");
}

    // Orbes que da un nivel segun sus estrellas.
    //
    // Es la misma tabla que usa SheepGDRequest para rellenar el campo Orbs del JSON
    // (GeometryDashService.cs), copiada literalmente a proposito: el nivel que llega
    // por la cola trae ese numero y el placeholder lo calcula aqui, asi que si las
    // dos tablas no fueran identicas el mismo nivel mostraria totales distintos
    // segun por donde entrase. Esa incoherencia es peor, y mucho mas dificil de
    // ver, que un valor discutible.
    //
    // Y hay uno discutible: el nivel de 1 estrella probablemente deberia dar 0, no
    // 50. Se deja igual que en C# por lo anterior; si se cambia, hay que cambiar las
    // dos tablas.
    //
    // No se usa getBaseCurrencyForLevel de GD: en un 10 estrellas devuelve 400
    // cuando el maximo real son 500. No es "las orbes del nivel", y sigue sin
    // saberse que significa ese numero.
    //
    // Los platformers dan lunas y no estrellas, pero usan la misma tabla:
    // comprobado en el juego el 2026-09-05 (un platformer de 10 lunas da 0/500,
    // igual que un nivel de 10 estrellas).
    inline int orbsForStars(int stars) {
        switch (stars) {
            case 1:  return 50;
            case 2:  return 50;
            case 3:  return 75;
            case 4:  return 125;
            case 5:  return 175;
            case 6:  return 225;
            case 7:  return 275;
            case 8:  return 350;
            case 9:  return 425;
            case 10: return 500;
            default: return 0;
        }
    }

} // namespace sheep