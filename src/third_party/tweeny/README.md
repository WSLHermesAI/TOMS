# Tweeny easing (vendored)

`easing.h` is the easing-function header of **Tweeny** (https://github.com/mobius3/tweeny), MIT licence
(the notice is at the top of the file), copied unchanged from the FM79979 engine's
`FM79979Engine/Tween/Tweeny/easing.h`. Only this header is used: the animation runtime
(`src/core/engine/anim_clip.cpp`) applies these easings to the progress between two keys. It needs
C++17 (inline `static constexpr` members) and nothing but `<cmath>` / `<type_traits>`.
