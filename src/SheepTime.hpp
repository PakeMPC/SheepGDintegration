#pragma once
#include <Geode/Geode.hpp>
#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// Utilidades de tiempo: el reloj compartido con SheepGDRequest (nowUnix), el
// formato del reloj del auto-skip y el enfriamiento entre cambios de nivel.
// ---------------------------------------------------------------------------
namespace sheep {

    // El instante actual, en segundos desde la epoca Unix (UTC). Es exactamente la
    // misma escala que escribe C# con DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
    // asi que los dos procesos comparan instantes sin problemas de husos horarios.
    inline int64_t nowUnix() {
        using namespace std::chrono;
        return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }

    // Segundos que faltan para 'endsAt'. 0 si ya paso o si no hay temporizador
    // (endsAt <= 0).
    inline int64_t secondsLeft(int64_t endsAt) {
        if (endsAt <= 0) return 0;
        auto left = endsAt - nowUnix();
        return left > 0 ? left : 0;
    }

    // 754 -> "12:34".  4530 -> "1:15:30".
    // Sin cero a la izquierda en la unidad mas grande, que es como se suele leer.
    inline std::string formatClock(int64_t totalSeconds) {
        if (totalSeconds < 0) totalSeconds = 0;
        auto h = totalSeconds / 3600;
        auto m = (totalSeconds % 3600) / 60;
        auto s = totalSeconds % 60;
        if (h > 0) return fmt::format("{}:{:02d}:{:02d}", h, m, s);
        return fmt::format("{}:{:02d}", m, s);
    }

    // -----------------------------------------------------------------------
    // Enfriamiento del cambio de nivel.
    //
    // Cada salto reescribe queue_data.json, y a partir de ahi se pone en marcha
    // todo lo demas: el FileSystemWatcher de C# relee el archivo y lo reemite por
    // WebSocket, el vigia del PlayLayer ve el requestId nuevo y lanza el nivel, y
    // el precargador cambia de objetivo a mitad de descarga. Nada de eso es
    // instantaneo, asi que encadenar varios saltos en el mismo segundo pone todo
    // el flujo a competir consigo mismo.
    //
    // Un segundo no se nota para quien pulsa una vez, y corta las pulsaciones
    // repetidas.
    // -----------------------------------------------------------------------
    inline constexpr double kSkipCooldown = 1.0;   // segundos

    namespace detail {
        // Instante del ultimo salto. steady_clock y no system_clock: este reloj no
        // puede saltar hacia atras si el sistema ajusta la hora, y aqui solo
        // interesan intervalos. (Al contrario que nowUnix, que necesita la escala
        // real porque la comparte con C# a traves del JSON.)
        inline std::chrono::steady_clock::time_point& lastSkip() {
            static std::chrono::steady_clock::time_point t{};
            return t;
        }
    }

    // Indica si ya se puede saltar de nivel. Si se puede, apunta el instante y
    // devuelve true.
    //
    // Consume el turno, asi que solo se llama cuando el salto va a ocurrir de
    // verdad: un "siguiente" con la cola vacia no debe gastar el enfriamiento del
    // salto valido que venga detras.
    //
    // El estado es global a proposito, y no un campo de la escena: los sitios que
    // mueven la cola son las flechas de la escena y las teclas Q/E de la partida, y
    // comparten limite. Ademas, la escena se destruye al entrar al nivel, asi que
    // un contador suyo no sobreviviria al viaje.
    inline bool trySkip() {
        auto ahora   = std::chrono::steady_clock::now();
        auto& ultimo = detail::lastSkip();

        // El valor por defecto es la epoca del reloj, asi que el primer salto de la
        // sesion siempre pasa sin esperar.
        if (ultimo.time_since_epoch().count() != 0) {
            std::chrono::duration<double> transcurrido = ahora - ultimo;
            if (transcurrido.count() < kSkipCooldown) return false;
        }

        ultimo = ahora;
        return true;
    }

} // namespace sheep