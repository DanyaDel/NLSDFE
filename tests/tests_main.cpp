// Проверки без окна и GPU: сцена, физика, скрипты
#include "TestUtil.h"

int g_testsRun = 0;
int g_testsFailed = 0;

void runSceneTests();
void runUvTests();
void runPhysicsTests();
void runScriptTests();

int main() {
    runSceneTests();
    runUvTests();
    runPhysicsTests();
    runScriptTests();
    std::printf("\n%d checks, %d failed\n", g_testsRun, g_testsFailed);
    return g_testsFailed == 0 ? 0 : 1;
}
