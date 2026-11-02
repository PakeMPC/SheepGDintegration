#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Traducciones del mod: todo el texto visible y todos los logs salen de estas
// tablas, por llave. Tres idiomas: ingles, espanol y portugues.
//
// - tr("Llave") devuelve el texto en el idioma actual.
// - trf("Llave", args...) lo devuelve ademas formateado con fmt ("{}").
// - refreshLanguage(ruta) relee el idioma. Lo llama la escena en cada visita.
//
// El idioma no lo elige el mod: lo lee del settings.json de SheepGDRequest, que
// esta en la misma carpeta que queue_data.json. Asi el streamer lo configura en un
// solo sitio y el juego lo sigue.
//
// Este archivo no incluye QueueData.hpp a proposito, aunque la ruta salga de ahi:
// QueueData tiene texto que traducir (el dialogo de enlazar el archivo), asi que
// incluirse mutuamente seria un ciclo. Por eso refreshLanguage() recibe la ruta en
// vez de ir a buscarla, y de paso este archivo se queda sin dependencias del
// proyecto y lo puede incluir cualquiera.
//
// Al anadir una llave, hay que anadirla en las tres tablas, con los mismos
// marcadores {} en el mismo orden. Una llave repetida dentro de una tabla no da
// error: se queda la primera y la otra se ignora sin aviso.
// ---------------------------------------------------------------------------
namespace sheep {

namespace detail {

    // Las tres tablas: llave -> texto. El ingles es la base y el respaldo, al
    // contrario que en C# (alli la base es el espanol). Sin un settings.json legible, ingles.
    //
    // Algunos textos llevan etiquetas de color de GD (<cy>...</c>), que entienden
    // los avisos (FLAlertLayer).
    inline std::unordered_map<std::string, std::string> const& en() {
        static std::unordered_map<std::string, std::string> m = {
            // --- Tarjeta ---
            { "Card_By",            "By {}" },
            { "Card_SentBy",        "Sent By {}" },
            { "Card_Placeholder",   "PLACEHOLDER LEVEL" },
            { "Card_Waiting",       "WAITING FOR REQUESTS..." },

            // --- Botones y lista ---
            { "Btn_View",           "View" },
            { "Queue_Loading",      "LOADING..." },
            { "Queue_Empty",        "THE QUEUE IS EMPTY" },
            { "Queue_NoFile",       "NO FILE LINKED" },
            { "Queue_Title",        "NEXT LEVELS" },

            // --- Confirmacion de borrado ---
            { "Del_Title",          "Remove" },
            { "Del_Body",           "Remove the current level and skip to the next one?" },
            { "Del_Cancel",         "Cancel" },
            { "Del_Confirm",        "Remove" },

            // --- Avisos ---
            { "Alert_Ok",           "OK" },
            { "Alert_QueueEmpty",   "The queue is empty." },
            { "Alert_WriteFailed",  "Could not write <cy>queue_data.json</c>." },
            { "Alert_NotFound",     "The level was not found on the servers." },
            { "Alert_SearchFailed", "The level search failed." },
            { "Alert_DownloadFailed", "Could not download the level." },

            // --- Enlazar el archivo ---
            { "Link_Title",         "Set up SheepGD" },
            { "Link_Body",          "To show your levels you need to link the app.\nPlease select the <cy>queue_data.json</c> file." },
            { "Link_Cancel",        "Cancel" },
            { "Link_Browse",        "Browse" },
            { "Link_NoneTitle",     "Cancelled" },
            { "Link_NoneBody",      "No file was selected." },
            { "Link_OkTitle",       "Success" },
            { "Link_OkBody",        "Path linked successfully." },
            { "Link_JsonFilter",    "JSON Data" },

            // --- Ajustes ---
            { "Set_Title",          "SheepGD Settings" },
            { "Set_Timer",          "TIMER INGAME" },
            { "Set_LastTen",        "LAST 10s" },
            { "Set_Sheepy",         "USE SHEEPY ICON" },
            { "Set_OnlyReq",        "ONLY IN REQUESTS" },
            { "Set_EndOfQueue",     "WHEN THE QUEUE ENDS" },
            { "Set_LevelId",        "Level ID" },
            { "Set_SwapOnDeath",    "SWAP ON DEATH" },

            // --- Modos de fin de cola ---
            { "End_Stay",           "STAY IN LEVEL" },
            { "End_Exit",           "EXIT TO LIST" },
            { "End_Death",          "UNTIL YOU DIE" },
            { "End_Placeholder",    "PLACEHOLDER" },

            // --- Log ---
            { "Log_KeyQueueEmpty",     "Key: the queue is empty, nowhere to jump" },
            { "Log_KeyNoPrevious",     "Key: there is no previous level" },
            { "Log_KeyWriteFailed",    "Key: could not write queue_data.json" },
            { "Log_KeyNext",           "Key: next level" },
            { "Log_KeyPrev",           "Key: previous level" },
            { "Log_SkipTooFast",       "Too fast: one level change per second" },
            { "Log_SwapNeverHappened", "The jump never happened; watching again" },
            { "Log_SwapOnDeath",       "Level {} on hold: swapping on death" },
            { "Log_EndDetected",       "End of queue detected (mode {})" },
            { "Log_EndExit",           "End of queue: leaving to the list" },
            { "Log_EndOnDeath",        "End of queue: will leave on death" },
            { "Log_EndNoPlaceholder",  "End of queue: no placeholder ID, staying" },
            { "Log_EndPlaceholder",    "End of queue: going to placeholder {}" },
            { "Log_EndExitAfterDeath", "End of queue: leaving to the list after dying" },
            { "Log_JumpingTo",         "Jumping to level {}" },
            { "Log_NoSpriteFrame",      "No sprite frame found. Tried: {}" },
            { "Log_MissingOwnAsset",    "Missing own asset '{}' in resources/" },
            { "Log_MissingButtonAsset", "Missing GJ_button_01.png in resources/" },
            { "Log_DiffIdxFallback",    "idx {} has no frame; using {} ('{}')" },
            { "Log_DiffFrameMissing",   "Difficulty frame does not exist: '{}' (idx {})" },
            { "Log_UnknownPlatform",    "Platform with no icon of its own: '{}'" },
            { "Log_JumpPreloaded",      "Jump: level {} was already preloaded" },
            { "Log_WaitingMusic",       "Level {}: waiting for the music (limit {:.0f} s)" },
            { "Log_MusicReady",         "Level {}: music ready after {:.1f} s" },
            { "Log_MusicTimeout",       "Level {}: wait timed out, entering without music" },
            { "Log_SkipCancelled",      "Auto-skip cancelled, level {}: {}" },

            // Motivos de fallo del lanzador. SheepLevelLauncher::fail() recibe la
            // llave y la traduce.
            { "Fail_NotOnServers",      "it does not show up on the servers" },
            { "Fail_SearchFailed",      "the search failed" },
            { "Fail_EmptyDownload",     "the download came back empty" },
            { "Fail_DownloadFailed",    "the download failed" },
            { "Fail_Timeout",           "the servers did not answer in time" },
            { "Fail_LeftLevel",         "the player left the level" },

            { "Log_OrbsClamped",        "Orbs of level {}: awarded {} > total {}, clamped" },
            { "Log_ThumbFailed",        "Thumbnail failed ({}) for level {}" },
            { "Log_PlaceholderFailed",  "Placeholder {}: the search failed" },
            { "Log_PlaceholderReady",   "Placeholder {}: entry ready ({})" },
            { "Log_PrefetchYield",      "Prefetch: giving up the slot (level {})" },
            { "Log_PrefetchDone",       "Prefetch: level {} downloaded, requesting its audio" },
            { "Log_PrefetchTimeout",    "Prefetch: gave up waiting for level {}" },
            { "Log_PrefetchSearching",  "Prefetch: searching for level {} (attempt {})" },
            { "Log_QueueTruncated",     "Queue of {} levels: showing only the first {}" },
            { "Log_NoLinkedPath",       "No linked path: nothing is saved" },
            { "Log_PanelFallback",      "Missing sheep_panel.png in resources/, using GD's panel" },
            { "Log_PanelInsets",        "Panel: canvas={} points, inset={}" },
            { "Log_ContentUrl",         "Requesting the custom content URL (attempt {})" },
            { "Log_SongDownloading",    "Song {}: entry ready, downloading file (attempt {})" },
            { "Log_SongInfo",           "Song {}: requesting entry (attempt {})" },
            { "Log_SfxDownloading",     "SFX {}: downloading (attempt {})" },
            { "Log_ThumbCacheFailed",   "Could not cache the thumbnail {}" },
            { "Log_BadJson",            "Invalid queue_data.json: {}" },
            { "Log_CantOpenWrite",      "Could not open {} for writing" },
            { "Log_CantReplace",        "Could not replace queue_data.json: {}" },
            { "Log_CantWriteFile",      "Could not finish writing {}" },
            { "Log_UndoUnsaved",        "The change could not be saved to queue_data.json; undoing it" },
            { "Log_AutoDetectHit",      "Auto-detection: {} -> FOUND" },
            { "Log_AutoDetectMiss",     "Auto-detection: {} -> no" },
            { "Log_AutoDetectResult",   "Auto-detection: wine={} candidates={} result='{}'" },
            { "Log_SavedPathGone",      "The saved path no longer exists, using the automatic one: {}" },
            { "Log_Language",           "Language: '{}'" },

        };
        return m;
    }

    inline std::unordered_map<std::string, std::string> const& es() {
        static std::unordered_map<std::string, std::string> m = {
            { "Card_By",            "De {}" },
            { "Card_SentBy",        "Enviado por {}" },
            { "Card_Placeholder",   "NIVEL DE ESPERA" },
            { "Card_Waiting",       "ESPERANDO PETICIONES..." },

            { "Btn_View",           "Ver" },
            { "Queue_Loading",      "CARGANDO..." },
            { "Queue_Empty",        "LA LISTA ESTA VACIA" },
            { "Queue_NoFile",       "SIN ARCHIVO ENLAZADO" },
            { "Queue_Title",        "PROXIMOS NIVELES" },

            { "Del_Title",          "Eliminar" },
            { "Del_Body",           "Quitar el nivel actual y pasar al siguiente?" },
            { "Del_Cancel",         "Cancelar" },
            { "Del_Confirm",        "Eliminar" },

            { "Alert_Ok",           "OK" },
            { "Alert_QueueEmpty",   "La lista esta vacia." },
            { "Alert_WriteFailed",  "No se pudo escribir <cy>queue_data.json</c>." },
            { "Alert_NotFound",     "No se encontro el nivel en los servidores." },
            { "Alert_SearchFailed", "Fallo la busqueda del nivel." },
            { "Alert_DownloadFailed", "No se pudo descargar el nivel." },

            { "Link_Title",         "Configurar SheepGD" },
            { "Link_Body",          "Para mostrar tus niveles debes enlazar la aplicacion.\nSelecciona el archivo <cy>queue_data.json</c>." },
            { "Link_Cancel",        "Cancelar" },
            { "Link_Browse",        "Buscar Archivo" },
            { "Link_NoneTitle",     "Cancelado" },
            { "Link_NoneBody",      "No se selecciono ningun archivo." },
            { "Link_OkTitle",       "Hecho" },
            { "Link_OkBody",        "Ruta enlazada correctamente." },
            { "Link_JsonFilter",    "Datos JSON" },

            { "Set_Title",          "Ajustes de SheepGD" },
            { "Set_Timer",          "TEMPORIZADOR EN GAMEPLAY" },
            { "Set_LastTen",        "ULTIMOS 10s" },
            { "Set_Sheepy",         "CUBO DE SHEEPY" },
            { "Set_OnlyReq",        "SOLO EN REQUESTS" },
            { "Set_EndOfQueue",     "AL ACABARSE LA LISTA" },
            { "Set_LevelId",        "ID del nivel" },
            { "Set_SwapOnDeath",    "SIGUIENTE AL MORIR" },

            { "End_Stay",           "SEGUIR EN EL NIVEL" },
            { "End_Exit",           "SALIR A LA LISTA" },
            { "End_Death",          "HASTA MORIR" },
            { "End_Placeholder",    "NIVEL DE ESPERA" },

            // --- Log ---
            { "Log_KeyQueueEmpty",     "Tecla: la cola esta vacia, no hay a donde saltar" },
            { "Log_KeyNoPrevious",     "Tecla: no hay nivel anterior" },
            { "Log_KeyWriteFailed",    "Tecla: no se pudo escribir queue_data.json" },
            { "Log_KeyNext",           "Tecla: siguiente nivel" },
            { "Log_KeyPrev",           "Tecla: nivel anterior" },
            { "Log_SkipTooFast",       "Demasiado rapido: un cambio de nivel por segundo" },
            { "Log_SwapNeverHappened", "El salto no llego a ocurrir; se vuelve a vigilar" },
            { "Log_SwapOnDeath",       "Nivel {} en espera: se cambia al morir" },
            { "Log_EndDetected",       "Fin de cola detectado (modo {})" },
            { "Log_EndExit",           "Fin de cola: saliendo a la lista" },
            { "Log_EndOnDeath",        "Fin de cola: se saldra al morir" },
            { "Log_EndNoPlaceholder",  "Fin de cola: no hay ID de placeholder, me quedo" },
            { "Log_EndPlaceholder",    "Fin de cola: yendo al placeholder {}" },
            { "Log_EndExitAfterDeath", "Fin de cola: saliendo a la lista tras morir" },
            { "Log_JumpingTo",         "Saltando al nivel {}" },
            { "Log_NoSpriteFrame",      "Ningun sprite frame encontrado. Probados: {}" },
            { "Log_MissingOwnAsset",    "Falta el asset propio '{}' en resources/" },
            { "Log_MissingButtonAsset", "Falta GJ_button_01.png en resources/" },
            { "Log_DiffIdxFallback",    "idx {} sin frame; usando {} ('{}')" },
            { "Log_DiffFrameMissing",   "Frame de dificultad inexistente: '{}' (idx {})" },
            { "Log_UnknownPlatform",    "Plataforma sin icono propio: '{}'" },
            { "Log_JumpPreloaded",      "Salto: el nivel {} ya estaba precargado" },
            { "Log_WaitingMusic",       "Nivel {}: esperando la musica (tope {:.0f} s)" },
            { "Log_MusicReady",         "Nivel {}: musica lista tras {:.1f} s" },
            { "Log_MusicTimeout",       "Nivel {}: se agoto la espera, entrando sin musica" },
            { "Log_SkipCancelled",      "Auto-skip cancelado, nivel {}: {}" },

            { "Fail_NotOnServers",      "no aparece en los servidores" },
            { "Fail_SearchFailed",      "fallo la busqueda" },
            { "Fail_EmptyDownload",     "la descarga vino vacia" },
            { "Fail_DownloadFailed",    "fallo la descarga" },
            { "Fail_Timeout",           "los servidores no respondieron a tiempo" },
            { "Fail_LeftLevel",         "el jugador salio del nivel" },

            { "Log_OrbsClamped",        "Orbes del nivel {}: ganadas {} > total {}, se recorta" },
            { "Log_ThumbFailed",        "Miniatura fallo ({}) para el nivel {}" },
            { "Log_PlaceholderFailed",  "Placeholder {}: fallo la busqueda" },
            { "Log_PlaceholderReady",   "Placeholder {}: ficha lista ({})" },
            { "Log_PrefetchYield",      "Precarga: cediendo el turno (nivel {})" },
            { "Log_PrefetchDone",       "Precarga: nivel {} descargado, pidiendo su audio" },
            { "Log_PrefetchTimeout",    "Precarga: se agoto la espera del nivel {}" },
            { "Log_PrefetchSearching",  "Precarga: buscando el nivel {} (intento {})" },
            { "Log_QueueTruncated",     "Lista de {} niveles: mostrando solo los primeros {}" },
            { "Log_NoLinkedPath",       "Sin ruta enlazada: no se guarda nada" },
            { "Log_PanelFallback",      "Falta sheep_panel.png en resources/, se usa el panel de GD" },
            { "Log_PanelInsets",        "Panel: lienzo={} puntos, inset={}" },
            { "Log_ContentUrl",         "Pidiendo la URL del contenido personalizado (intento {})" },
            { "Log_SongDownloading",    "Cancion {}: ficha lista, descargando archivo (intento {})" },
            { "Log_SongInfo",           "Cancion {}: pidiendo ficha (intento {})" },
            { "Log_SfxDownloading",     "SFX {}: descargando (intento {})" },
            { "Log_ThumbCacheFailed",   "No se pudo cachear la miniatura {}" },
            { "Log_BadJson",            "queue_data.json invalido: {}" },
            { "Log_CantOpenWrite",      "No se pudo abrir {} para escribir" },
            { "Log_CantReplace",        "No se pudo reemplazar queue_data.json: {}" },
            { "Log_CantWriteFile",      "No se pudo terminar de escribir {}" },
            { "Log_UndoUnsaved",        "No se pudo guardar el cambio en queue_data.json; se deshace" },
            { "Log_AutoDetectHit",      "Auto-deteccion: {} -> ENCONTRADO" },
            { "Log_AutoDetectMiss",     "Auto-deteccion: {} -> no" },
            { "Log_AutoDetectResult",   "Auto-deteccion: wine={} candidatos={} resultado='{}'" },
            { "Log_SavedPathGone",      "La ruta guardada ya no existe, se usa la automatica: {}" },
            { "Log_Language",           "Idioma: '{}'" },
        };
        return m;
    }

    inline std::unordered_map<std::string, std::string> const& pt() {
        static std::unordered_map<std::string, std::string> m = {
            { "Card_By",            "De {}" },
            { "Card_SentBy",        "Pedido por {}" },
            { "Card_Placeholder",   "NIVEL DE ESPERA" },
            { "Card_Waiting",       "ESPERANDO PEDIDOS..." },

            { "Btn_View",           "Ver" },
            { "Queue_Loading",      "CARREGANDO..." },
            { "Queue_Empty",        "A FILA ESTA VAZIA" },
            { "Queue_NoFile",       "SEM ARQUIVO VINCULADO" },
            { "Queue_Title",        "PROXIMOS NIVEIS" },

            { "Del_Title",          "Remover" },
            { "Del_Body",           "Remover o nivel atual e passar para o proximo?" },
            { "Del_Cancel",         "Cancelar" },
            { "Del_Confirm",        "Remover" },

            { "Alert_Ok",           "OK" },
            { "Alert_QueueEmpty",   "A fila esta vazia." },
            { "Alert_WriteFailed",  "Nao foi possivel escrever <cy>queue_data.json</c>." },
            { "Alert_NotFound",     "O nivel nao foi encontrado nos servidores." },
            { "Alert_SearchFailed", "A busca do nivel falhou." },
            { "Alert_DownloadFailed", "Nao foi possivel baixar o nivel." },

            { "Link_Title",         "Configurar SheepGD" },
            { "Link_Body",          "Para mostrar seus niveis precisa vincular o aplicativo.\nSelecione o arquivo <cy>queue_data.json</c>." },
            { "Link_Cancel",        "Cancelar" },
            { "Link_Browse",        "Procurar" },
            { "Link_NoneTitle",     "Cancelado" },
            { "Link_NoneBody",      "Nenhum arquivo foi selecionado." },
            { "Link_OkTitle",       "Sucesso" },
            { "Link_OkBody",        "Arquivo vinculado com sucesso." },
            { "Link_JsonFilter",    "Dados JSON" },

            { "Set_Title",          "Ajustes do SheepGD" },
            { "Set_Timer",          "RELOGIO NA PARTIDA" },
            { "Set_LastTen",        "ULTIMOS 10s" },
            { "Set_Sheepy",         "CUBO DA SHEEPY" },
            { "Set_OnlyReq",        "SO EM REQUESTS" },
            { "Set_EndOfQueue",     "QUANDO A FILA ACABAR" },
            { "Set_LevelId",        "ID do nivel" },
            { "Set_SwapOnDeath",    "TROCAR AO MORRER" },

            { "End_Stay",           "FICAR NO NIVEL" },
            { "End_Exit",           "SAIR PARA A LISTA" },
            { "End_Death",          "ATE MORRER" },
            { "End_Placeholder",    "NIVEL DE ESPERA" },

            // --- Log ---
            { "Log_KeyQueueEmpty",     "Tecla: a fila esta vazia, nao ha para onde pular" },
            { "Log_KeyNoPrevious",     "Tecla: nao ha nivel anterior" },
            { "Log_KeyWriteFailed",    "Tecla: nao foi possivel escrever queue_data.json" },
            { "Log_KeyNext",           "Tecla: proximo nivel" },
            { "Log_KeyPrev",           "Tecla: nivel anterior" },
            { "Log_SkipTooFast",       "Rapido demais: uma troca de nivel por segundo" },
            { "Log_SwapNeverHappened", "A troca nao chegou a acontecer; voltando a vigiar" },
            { "Log_SwapOnDeath",       "Nivel {} em espera: troca ao morrer" },
            { "Log_EndDetected",       "Fim da fila detectado (modo {})" },
            { "Log_EndExit",           "Fim da fila: saindo para a lista" },
            { "Log_EndOnDeath",        "Fim da fila: vai sair ao morrer" },
            { "Log_EndNoPlaceholder",  "Fim da fila: sem ID de nivel de espera, fico aqui" },
            { "Log_EndPlaceholder",    "Fim da fila: indo para o nivel de espera {}" },
            { "Log_EndExitAfterDeath", "Fim da fila: saindo para a lista apos morrer" },
            { "Log_JumpingTo",         "Pulando para o nivel {}" },
            { "Log_NoSpriteFrame",      "Nenhum sprite frame encontrado. Testados: {}" },
            { "Log_MissingOwnAsset",    "Falta o asset proprio '{}' em resources/" },
            { "Log_MissingButtonAsset", "Falta GJ_button_01.png em resources/" },
            { "Log_DiffIdxFallback",    "idx {} sem frame; usando {} ('{}')" },
            { "Log_DiffFrameMissing",   "Frame de dificuldade inexistente: '{}' (idx {})" },
            { "Log_UnknownPlatform",    "Plataforma sem icone proprio: '{}'" },
            { "Log_JumpPreloaded",      "Troca: o nivel {} ja estava pre-carregado" },
            { "Log_WaitingMusic",       "Nivel {}: esperando a musica (limite {:.0f} s)" },
            { "Log_MusicReady",         "Nivel {}: musica pronta apos {:.1f} s" },
            { "Log_MusicTimeout",       "Nivel {}: a espera esgotou, entrando sem musica" },
            { "Log_SkipCancelled",      "Auto-skip cancelado, nivel {}: {}" },

            { "Fail_NotOnServers",      "nao aparece nos servidores" },
            { "Fail_SearchFailed",      "a busca falhou" },
            { "Fail_EmptyDownload",     "o download veio vazio" },
            { "Fail_DownloadFailed",    "o download falhou" },
            { "Fail_Timeout",           "os servidores nao responderam a tempo" },
            { "Fail_LeftLevel",         "o jogador saiu do nivel" },

            { "Log_OrbsClamped",        "Orbes do nivel {}: ganhas {} > total {}, foi cortado" },
            { "Log_ThumbFailed",        "Miniatura falhou ({}) para o nivel {}" },
            { "Log_PlaceholderFailed",  "Nivel de espera {}: a busca falhou" },
            { "Log_PlaceholderReady",   "Nivel de espera {}: ficha pronta ({})" },
            { "Log_PrefetchYield",      "Pre-carga: cedendo a vez (nivel {})" },
            { "Log_PrefetchDone",       "Pre-carga: nivel {} baixado, pedindo o audio dele" },
            { "Log_PrefetchTimeout",    "Pre-carga: a espera do nivel {} esgotou" },
            { "Log_PrefetchSearching",  "Pre-carga: buscando o nivel {} (tentativa {})" },
            { "Log_QueueTruncated",     "Fila de {} niveis: mostrando so os primeiros {}" },
            { "Log_NoLinkedPath",       "Sem caminho vinculado: nada e salvo" },
            { "Log_PanelFallback",      "Falta sheep_panel.png em resources/, usando o painel do GD" },
            { "Log_PanelInsets",        "Painel: tela={} pontos, inset={}" },
            { "Log_ContentUrl",         "Pedindo a URL do conteudo personalizado (tentativa {})" },
            { "Log_SongDownloading",    "Musica {}: ficha pronta, baixando o arquivo (tentativa {})" },
            { "Log_SongInfo",           "Musica {}: pedindo a ficha (tentativa {})" },
            { "Log_SfxDownloading",     "SFX {}: baixando (tentativa {})" },
            { "Log_ThumbCacheFailed",   "Nao foi possivel armazenar a miniatura {}" },
            { "Log_BadJson",            "queue_data.json invalido: {}" },
            { "Log_CantOpenWrite",      "Nao foi possivel abrir {} para escrever" },
            { "Log_CantReplace",        "Nao foi possivel substituir queue_data.json: {}" },
            { "Log_CantWriteFile",      "Nao foi possivel terminar de escrever {}" },
            { "Log_UndoUnsaved",        "Nao foi possivel salvar a mudanca em queue_data.json; desfazendo" },
            { "Log_AutoDetectHit",      "Auto-deteccao: {} -> ENCONTRADO" },
            { "Log_AutoDetectMiss",     "Auto-deteccao: {} -> nao" },
            { "Log_AutoDetectResult",   "Auto-deteccao: wine={} candidatos={} resultado='{}'" },
            { "Log_SavedPathGone",      "O caminho salvo nao existe mais, usando o automatico: {}" },
            { "Log_Language",           "Idioma: '{}'" },

        };
        return m;
    }

    // Idioma actual: "en", "es" o "pt". Lo cambia refreshLanguage.
    inline std::string& currentLang() { static std::string v = "en"; return v; }

} // namespace detail

// El texto de una llave. Orden de respaldo: idioma actual -> ingles -> "[Llave]".
//
// Lo ultimo es a proposito, y copiado de C#: una llave que falte tiene que verse.
// Devolver el ingles sin avisar esconderia el olvido hasta que alguien tradujera
// la interfaz entera y notase que una linea nunca cambia.
inline std::string tr(std::string const& key) {
    auto const& tabla = detail::currentLang() == "es" ? detail::es()
                      : detail::currentLang() == "pt" ? detail::pt()
                                                      : detail::en();

    if (auto it = tabla.find(key); it != tabla.end()) return it->second;
    if (auto it = detail::en().find(key); it != detail::en().end()) return it->second;
    return "[" + key + "]";
}

    // Traduce y formatea. Existe por un detalle de fmt: comprueba las cadenas de
    // formato en tiempo de compilacion, asi que una que sale de una tabla en
    // ejecucion hay que envolverla en fmt::runtime o no compila.
    //
    // Uso: log::info("[SheepGD] {}", sheep::trf("Log_JumpingTo", id));
    //
    // Nota sobre los tipos de los bindings: log::info y similares aceptan mas tipos
    // que fmt::format, porque Geode sabe formatear sus propios tipos. Al pasar por
    // aqui eso se pierde: lo que llegue tiene que ser algo que fmt conozca. El caso
    // que ya dio problemas es GJGameLevel::m_levelID, que no es un int sino un
    // geode::SeedValueRSV, y hay que pasarle .value(). Regla: si el argumento sale
    // de un binding y no es un tipo del lenguaje, convertirlo antes.
    //
    // Y por eso mismo lleva try/catch: al no comprobarse en compilacion, una
    // traduccion con los {} descuadrados (sobran, faltan o el tipo no encaja) ya no
    // es un error de compilacion sino una excepcion en ejecucion, y una linea de
    // log no puede tumbar el mod. Si pasa, sale el patron sin rellenar y un aviso
    // con la llave exacta que hay que corregir. Ese aviso va con texto fijo (en
    // ingles) y no por la tabla, para no depender de ella justo cuando la tabla
    // esta fallando: es una de las excepciones a la regla de los logs por llave.
    template <class... Args>
    inline std::string trf(std::string const& key, Args const&... args) {
        auto pattern = tr(key);
        try {
            return fmt::format(fmt::runtime(pattern), args...);
        } catch (std::exception const&) {
            log::warn("[SheepGD] Translation with an invalid format: '{}'", key);
            return pattern;
        }
    }

// Relee el idioma desde el settings.json de SheepGDRequest.
//
// 'queueJson' es la ruta de queue_data.json; el settings.json esta en su misma
// carpeta. Si algo falla (no hay ruta, no existe el archivo, el JSON esta roto, o
// el idioma viene vacio porque la app todavia no lo ha preguntado), se queda en
// ingles, que es lo que se pidio.
//
// Los textos que ya estan en pantalla no cambian solos: se aplica a lo que se
// cree despues. Por eso la escena lo llama al principio de su init.
//
// Va detras de tr y trf porque usa trf para su propio log.
inline void refreshLanguage(std::filesystem::path const& queueJson) {
    detail::currentLang() = "en";
    if (queueJson.empty()) return;

    auto file = queueJson.parent_path() / "settings.json";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || ec) return;

    std::ifstream in(file);
    auto parsed = matjson::parse(in);
    if (parsed.isErr()) return;

    // "AppLanguage" en PascalCase: SettingsService serializa con System.Text.Json
    // sin politica de nombres, asi que el nombre en el JSON es el de la propiedad
    // tal cual. No es camelCase, aunque lo parezca.
    auto lang = parsed.unwrap()["AppLanguage"].asString().unwrapOr("");
    for (auto& c : lang) c = (char)std::tolower((unsigned char)c);

    if (lang == "es" || lang == "pt") detail::currentLang() = lang;

    log::debug("[SheepGD] {}", trf("Log_Language", detail::currentLang()));
}

} // namespace sheep