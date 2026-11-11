// Model: SDF-моделлер одного ассета. Те же инструменты, что в сцене,
// но без игровых компонентов — модель хранит только форму и материалы.
#include "Panels.h"

void drawModelWorkspace(GLFWwindow* window, const WorkArea& area) {
    drawEditingLayout(window, area, false);
}
