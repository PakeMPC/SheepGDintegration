#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <optional>
#include <string>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Utilidades de dibujo y maquetacion:
//
// - Rectangulos redondeados. Cocos no sabe dibujarlos: se aproximan con segmentos
//   (roundedRectPoints) y se rellenan con CCDrawNode. El marco de la tarjeta de
//   Now Playing es un aro entre dos de ellos (drawRing).
// - Filas de "icono + texto" para las estadisticas de un nivel (layoutIconTextRow).
// ---------------------------------------------------------------------------
namespace sheep {

    // Tramos rectos con los que se aproxima cada esquina (un cuarto de circulo).
    inline constexpr int kCornerSegments = 8;

    // Contorno de un rectangulo redondeado de tamano 'size' con su esquina inferior
    // izquierda en 'origin', como poligono convexo en sentido antihorario.
    //
    // Cada esquina es un cuarto de circulo de 'segments' tramos, empezando por la
    // de arriba a la derecha. El radio se recorta a la mitad del lado menor, para
    // que dos esquinas nunca se crucen. Devuelve 4 * (segments + 1) puntos.
    inline std::vector<CCPoint> roundedRectPoints(CCSize const& size, float radius,
                                                  CCPoint origin = {0.f, 0.f},
                                                  int segments = kCornerSegments) {
        radius = std::min(radius, std::min(size.width, size.height) / 2.f);

        // Centro de cada esquina y angulo en el que empieza su cuarto de circulo.
        struct Corner { CCPoint center; float startDeg; };
        const Corner corners[4] = {
            {{size.width - radius, size.height - radius},   0.f},
            {{radius,              size.height - radius},  90.f},
            {{radius,              radius},               180.f},
            {{size.width - radius, radius},               270.f},
        };

        std::vector<CCPoint> pts;
        pts.reserve(4 * (segments + 1));

        for (auto const& k : corners) {
            for (int i = 0; i <= segments; ++i) {
                float a = CC_DEGREES_TO_RADIANS(k.startDeg + 90.f * i / segments);
                pts.push_back({origin.x + k.center.x + radius * cosf(a),
                               origin.y + k.center.y + radius * sinf(a)});
            }
        }
        return pts;
    }

    // Aro entre dos contornos concentricos, como el que hace de marco.
    //
    // Se dibuja como un cuadrilatero por tramo, uniendo el punto i y el i+1 de los
    // dos contornos, porque drawPolygon de CCDrawNode solo rellena bien poligonos
    // convexos y el aro entero no lo es. Los dos contornos deben tener el mismo
    // numero de puntos, es decir, venir de roundedRectPoints con los mismos
    // 'segments'; si no, se usa el menor de los dos y el aro sale mal.
    inline void drawRing(CCDrawNode* node,
                         std::vector<CCPoint> const& outer,
                         std::vector<CCPoint> const& inner,
                         ccColor4F const& color) {
        const size_t n = std::min(outer.size(), inner.size());
        for (size_t i = 0; i < n; ++i) {
            const size_t j = (i + 1) % n;
            CCPoint quad[4] = { outer[i], outer[j], inner[j], inner[i] };
            node->drawPolygon(quad, 4, color, 0.f, ccc4f(0, 0, 0, 0));
        }
    }

    // Un elemento de una fila de estadisticas: un icono, un texto, o los dos.
    struct RowItem {
        cocos2d::CCSprite* icon = nullptr;
        std::string text;
        cocos2d::ccColor3B color = {255, 255, 255};   // color del texto
        // Separacion tras este elemento. Vacio = usar blockGap; con valor (puede ser
        // negativo) se usa ese. Hoy ninguna llamada lo usa.
        std::optional<float> gapAfter;
    };

    // Coloca [icono][texto] [icono][texto]... dentro de 'row', de izquierda a
    // derecha desde x = 0 y centrados en y = 0.
    //
    // - iconHeight: alto al que se escala cada icono (el ancho sigue su proporcion).
    // - labelScale: escala de los textos, en bigFont.fnt.
    // - iconGap: separacion entre el icono y su texto.
    // - blockGap: separacion entre un elemento y el siguiente.
    //
    // Devuelve el ancho total sin escalar, para que quien llama decida si encoge la
    // fila. Es lo que ocupa de verdad: la separacion solo va entre elementos, no
    // tras el ultimo, o quien llama encogeria la fila aunque cupiera.
    inline float layoutIconTextRow(cocos2d::CCNode* row, std::vector<RowItem> const& items,
                                   float iconHeight, float labelScale,
                                   float iconGap, float blockGap) {
        float x = 0.f;
        for (size_t i = 0; i < items.size(); ++i) {
            auto const& it = items[i];
            if (it.icon) {
                float s = iconHeight / std::max(1.f, it.icon->getContentSize().height);
                it.icon->setScale(s);
                it.icon->setAnchorPoint({0.f, 0.5f});
                it.icon->setPosition({x, 0.f});
                row->addChild(it.icon);
                x += it.icon->getContentSize().width * s;
                if (!it.text.empty()) x += iconGap;
            }
            if (!it.text.empty()) {
                auto label = cocos2d::CCLabelBMFont::create(it.text.c_str(), "bigFont.fnt");
                label->setAnchorPoint({0.f, 0.5f});
                label->setScale(labelScale);
                label->setColor(it.color);
                label->setPosition({x, 0.f});
                row->addChild(label);
                x += label->getContentSize().width * labelScale;
            }
            if (i + 1 < items.size()) x += it.gapAfter.value_or(blockGap);
        }
        return x;
    }

} // namespace sheep