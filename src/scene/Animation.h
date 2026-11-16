#pragma once
#include "Common.h"

// Анимация узла по ключевым кадрам (компонент Animation)
namespace anim {

float length(const NodeAnimation& a);                    // Время последнего ключа
Keyframe sample(const NodeAnimation& a, float time);     // Плавная интерполяция между соседними ключами
// Ставит ключ в нужное место по времени; ключ с тем же временем заменяется. Возвращает его индекс.
int insertKey(NodeAnimation& a, const Keyframe& key);
Keyframe keyFromNode(const SceneNode& node, float time); // Текущая локальная трансформация как ключ
void apply(SceneNode& node, float time);                 // Ставит узел в позу на момент time

void play(SceneNode& node, bool restart);
void stop(SceneNode& node);
// Продвигает играющие анимации сцены и применяет позы
void update(float dt);
// Старт игры: анимации с playOnStart начинают играть с нуля
void startAll();

}
