#include "EngineBridge.h"
#include "EngineExports.h"
#include "Common.h"

// Только эти функции требуют доступа к движку (main.cpp) для работы с файлами/скриншотами
void saveNos(const std::string& path) { Engine_PublicSaveNos(path); }
void loadNos(const std::string& path) { Engine_PublicLoadNos(path, false); }
void appendNos(const std::string& path) { Engine_PublicLoadNos(path, true); }
void loadMesh(const std::string& path) { Engine_LoadMesh(path); }
void* createUiTexture(const uint8_t* rgba, int w, int h) { return Engine_CreateUiTexture(rgba, w, h); }
void destroyUiTexture(void* textureId) { Engine_DestroyUiTexture(textureId); }
bool exportGame(const std::string& directory) { return Engine_ExportGame(directory); }

// Остальные функции (resetScene, deleteSelectedObject и т.д.) 
// удалены отсюда, так как они теперь реализованы напрямую в Scene.cpp
