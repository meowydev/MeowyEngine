# Regression assets

The glTF triangles are original, embedded-data fixtures for scene filtering,
skeletal translation, rotation, scale, STEP and CUBICSPLINE sampling.

`test-tone.ogg`, `test-tone.mp3`, and `test-tone.flac` are a generated 0.1-second,
440 Hz sine wave at 48 kHz. They contain no third-party recording. The Ogg
fixture is stereo; the MP3 and FLAC fixtures are mono.

The tone comes from `samples/audio_checks.cpp` and was encoded with FFmpeg's
native experimental Vorbis, libmp3lame, and FLAC encoders. FFmpeg is only needed
to regenerate these fixtures, not to build or run MeowyRender.
