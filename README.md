# UMR (Unity Media Recorder)

Record audio and video from ```Camera``` and ```AudioListener``` in Unity, encoding and muxing with FFmpeg on background threads.

# Usage
- Add ```./Unity/``` to Unity project as a Unity package
- Add ```Recorder``` to a ```GameObject``` with a ```Camera``` and/or an ```AudioListener```
- Call ```Recorder.Begin(filenameWithoutExtension)``` to begin recording
- Call ```Recorder.End()``` to end recording

# Notes:
- Tested with:
  - Windows 11 Pro 10.0.26200 Build 26200
  - Visual Studio 2026 18.6.2
  - Unity 6000.3.15f1
  - FFmpeg 8.1:
    - libavcodec 62.28.101
    - libavformat 62.12.102
    - libavutil 60.26.101
    - libswresample 6.3.101
    - libswscale 9.5.101
  - System.Threading.Channels 10.0.8
- The C++ codebase (```./cpp/```) includes Win64 LGPL builds of FFmpeg libraries from [BtbN](https://github.com/BtbN/FFmpeg-Builds/releases)
- The Unity package (```./Unity/```) includes the same builds of FFmpeg libraries, as well as a Win64 build of the C++ library and a .NET Standard 2.1 build of System.Threading.Channels from [NuGet](https://www.nuget.org/packages/system.threading.channels/)
