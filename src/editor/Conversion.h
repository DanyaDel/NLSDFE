#pragma once
#include "Common.h"

// Object > Convert: SDF <-> Mesh для выделенных объектов
enum ConversionKind { CONVERT_TO_MESH, CONVERT_TO_VOLUME };

bool canConvert(ConversionKind kind);      // Есть ли в выделении что конвертировать
void requestConversion(ConversionKind kind);   // Открывает окно с настройками
void drawConversionPopup();                // Раз в кадр
bool runConversion(ConversionKind kind, int resolution, bool keepOriginals);   // Без окна (командная строка)
