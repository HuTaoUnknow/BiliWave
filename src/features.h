#pragma once

#ifndef BILI_ENABLE_LYRICS
#define BILI_ENABLE_LYRICS 1
#endif

constexpr bool lyricFeaturesEnabled() {
    return BILI_ENABLE_LYRICS != 0;
}
#ifndef BILI_ENABLE_RECOGNITION
#define BILI_ENABLE_RECOGNITION BILI_ENABLE_LYRICS
#endif
constexpr bool recognitionFeaturesEnabled() {
    return lyricFeaturesEnabled() && BILI_ENABLE_RECOGNITION != 0;
}
