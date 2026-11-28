#pragma once

#include "core/HandLandmarker.h"

namespace sl
{
std::unique_ptr<HandLandmarker> makeVisionHandLandmarker();
}
