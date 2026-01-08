#pragma once
#include "Common.h"

GizmoAxis checkGizmoIntersection(Ray ray, vec3 pivotPos, vec3 camPos);
float getMouseDeltaOnAxis(vec3 axisWorldDir, float dx, float dy);
Ray getRayFromMouse(float mx, float my, float width, float height);

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

void processInput(GLFWwindow* window, float deltaTime);
