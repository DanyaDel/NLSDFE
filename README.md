# 🌀 SDF Engine (NLSDFE) - v1.1

**SDF Engine** — это легковесный 3D-редактор, использующий магию **Signed Distance Fields (SDF)**. В отличие от обычных движков, здесь нет полигонов и треугольников. Все объекты — это чистая математика, просчитываемая на видеокарте в реальном времени.

![Graphics API](https://img.shields.io/badge/Graphics-Vulkan-red.svg)
![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

---

## 🚀 Основные фишки

*   **Vulkan Compute Renderer:** Вся отрисовка происходит в Compute-шейдерах. Это позволяет создавать идеально гладкие формы и сложные математические фракталы.
*   **CSG Operations:** Поддержка логических операций над объектами:
    *   *Union* (Объединение)
    *   *Subtraction* (Вычитание — "дырки" в объектах)
    *   *Intersection* (Пересечение)
    *   *Smooth Blending* (Плавное «слипание» объектов в стиле ртути).
*   **Blender-style Workflow:** 
    *   Навигация WASD + Мышь.
    *   Профессиональные Гизмо (Move, Rotate, Scale).
    *   Горячие клавиши для продуктивности.
*   **Viewport Shading:** Четыре режима отображения:
    *   **W** (Wireframe/Heatmap) — анализ производительности шейдера.
    *   **S** (Solid) — быстрая работа с формами.
    *   **M** (Material) — предварительный просмотр цветов.
    *   **R** (Rendered) — финальная картинка с мягкими тенями и PBR-освещением.
*   **Система уведомлений (Toasts):** Интерактивные сообщения о копировании, вставке и ошибках.
*   **Формат .NOS:** Собственный бинарный формат сохранения сцен с поддержкой превью-скриншотов.

---

## 🛠 Технологии

*   **Язык:** C++17
*   **Графика:** Vulkan SDK 1.3+
*   **Интерфейс:** Dear ImGui
*   **Окно/Ввод:** GLFW3
*   **Сборка:** CMake

---

## ⌨️ Управление (Hotkeys)

| Клавиша | Действие |
| :--- | :--- |
| **W / A / S / D** | Движение камеры |
| **Shift** (зажать) | Ускорение камеры |
| **1 / 2 / 3** | Режимы Гизмо (Move / Rotate / Scale) |
| **Ctrl + C / V** | Копировать / Вставить объект |
| **Alt + D** | Быстрый дубликат |
| **Ctrl + Z** | Отмена действия (Undo) |
| **Delete** | Удалить выбранный объект |
| **F5** | Перезагрузить шейдеры "на лету" |

---

## ⚙️ Как собрать проект

### Требования:
1.  Установленный **Vulkan SDK**.
2.  Компилятор C++ (MSVC / Visual Studio 2022 рекомендуемо).
3.  **CMake** 3.10+.

### Инструкция:
1. Клонируй репозиторий:
   ```bash
   git clone https://github.com/твой_ник/NLSDFE.git
   cd NLSDFE
   ```
2. Сгенерируй проект через CMake:
   ```bash
   cmake -S . -B build
   ```
3. Скомпилируй шейдер (убедись, что `glslc` в PATH):
   ```bash
   glslc shaders/shader.comp -o build/Debug/shader.spv
   ```
4. Собери исполняемый файл:
   ```bash
   cmake --build build --config Debug
   ```

---

# 🗺 NLSDFE Development Roadmap

## 🟦 Phase 1: Core Architecture & Cleanup
*Goal: Transform "spaghetti" code into a scalable modular system.*
- [ ] **Modularization:** Split `main.cpp` into `Renderer.cpp`, `Scene.cpp`, `Editor.cpp`, and `Input.cpp`.
- [ ] **Scene Graph:** Implement Parent-Child hierarchy for object grouping.
- [ ] **Naming System:** Support for custom object names in the hierarchy panel.
- [ ] **Resource Manager:** Centralized handling for shaders, textures, and `.nos` files.

## 🟧 Phase 2: Advanced Rendering
*Goal: Maximize Vulkan and SDF mathematical potential.*
- [ ] **Multi-Light Support:** Implementation of up to 8 dynamic Point Lights.
- [ ] **Global Illumination (Lite):** Primitive screen-space or math-based bounce lighting.
- [ ] **Bloom Effect:** Post-processing pass for glowing materials and light sources.
- [ ] **Bounding Volumes:** Optimization to skip SDF calculations for distant objects.
- [ ] **Skybox Engine:** Procedural sky system or HDRI environment mapping.

## 🟩 Phase 3: Editor Workflow
*Goal: Fast and intuitive level design tools.*
- [ ] **Grid & Snapping:** Floor grid visualization and object snapping (0.5m / 1.0m steps).
- [ ] **Asset Browser:** Visual gallery of saved `.nos` models with drag-and-drop.
- [ ] **Material Library:** Presets for Gold, Glass, Plastic, and Custom shaders.
- [ ] **Multi-Selection:** Simultaneous transformation (move/rotate/scale) of multiple objects.
- [ ] **Advanced Undo/Redo:** Persistent action history with 100+ steps.

## 🟥 Phase 4: Gameplay & Physics
*Goal: Bringing the static world to life.*
- [ ] **SDF Collisions:** Prevent camera and player-controlled objects from passing through walls.
- [ ] **Trigger System:** Invisible volumes that fire events (e.g., door opening).
- [ ] **Scripting Components:** Attach logic to objects (e.g., auto-rotation, path following).
- [ ] **Play Mode (F5):** Instant switch to game mode with UI hidden and physics enabled.

## 🟪 Phase 5: Distribution & Polish
*Goal: Final product stabilization.*
- [ ] **Standalone Export:** Compile a scene into a single executable without the editor.
- [ ] **Settings Menu:** In-engine graphics settings (Resolution, Shadow Quality, TDR limits).
- [ ] **API Documentation:** Complete guide for hotkeys and developer instructions.

---

### 👥 Team Roles

**Graphics & Math Lead (You):**
- Focus on Phase 2 (Lights, Optimization, Shaders).
- Collision mathematics from Phase 4.
- Vulkan pipeline maintenance.

**Tooling & UX Lead (Friend):**
- Focus on Phase 1 (File splitting, Hierarchy logic).
- Phase 3 implementation (Grid, Asset browser, Material UI).
- General ImGui management.

---
*Development Roadmap for NLSDFE Engine - January 2026*
---

## 🐞 Список недавних исправлений (v1.1)

*   **Zero-Scale Guard:** Исправлен критический вылет при уменьшении масштаба объекта до нуля.
*   **Smart Inversion:** Теперь объекты можно инвертировать (зеркалить) через гизмо без артефактов "растягивания на всю карту".
*   **Shift-Conflict Fix:** Ускорение камеры на Shift больше не вызывает случайное дублирование объектов.
*   **UI Stability:** Исправлены ошибки Stack Underflow в ImGui при быстром переключении режимов.


---
*Сделано с помощью C++ и боли в Vulkan.*
