#pragma once
#include <string>

// Object > Edit in Blender: живая связка с Blender (аддон nlsdfe_link.py рядом с редактором)
bool canEditInBlender();
// В Model Editor уходит вся модель, в сцене — выделенное: SDF-объекты или один меш.
// background: прогнать аддон без окна (--selftest) и дождаться его — для проверок.
bool editInBlender(bool background = false);
void pollBlender();   // Раз в кадр: подхватывает сохранения из Blender
int blenderSessionCount();
