#version 450
// Writes normalized light-space depth into an R32F color attachment. Using a
// color target (instead of sampling a depth attachment) sidesteps MoltenVK
// depth-texture sampling quirks and keeps shadow lookups portable.
layout(location = 0) in float vDepth;
layout(location = 0) out float outDepth;
void main() { outDepth = vDepth; }
