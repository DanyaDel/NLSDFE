// Scene: сборка уровня из моделей, света и логики. Отсюда запускается Play Mode.
#include "Panels.h"

void drawSceneWorkspace(GLFWwindow* window, const WorkArea& area) {
    drawEditingLayout(window, area, true);
}
