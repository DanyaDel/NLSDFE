#include "EngineBridge.h"
#include "EngineExports.h"
#include "Common.h"

// Только эти функции требуют доступа к движку (main.cpp) для работы с файлами/скриншотами
void saveNos(const std::string& path) { Engine_PublicSaveNos(path); }
void loadNos(const std::string& path) { Engine_PublicLoadNos(path); }
void loadMesh(const std::string& path) { Engine_LoadMesh(path); }

// Остальные функции (resetScene, deleteSelectedObject и т.д.) 
// удалены отсюда, так как они теперь реализованы напрямую в Scene.cpp