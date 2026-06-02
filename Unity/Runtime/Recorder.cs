using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading.Channels;
using System.Threading.Tasks;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;
using UnityEngine.Rendering;

namespace UMR
{
  public class Recorder : MonoBehaviour
  {
    private const long AudioBitRate = 128000;
    private const long VideoBitRate = 8000000;
    private const int AudioBufferByteLimit = 32000000;
    private const int VideoBufferByteLimit = 256000000;

    private static readonly Dictionary<AudioSpeakerMode, int> s_channels = new()
    {
      [AudioSpeakerMode.Mono] = 1,
      [AudioSpeakerMode.Stereo] = 2,
      [AudioSpeakerMode.Prologic] = 2,
      [AudioSpeakerMode.Quad] = 4,
      [AudioSpeakerMode.Surround] = 5,
      [AudioSpeakerMode.Mode5point1] = 6,
      [AudioSpeakerMode.Mode7point1] = 8
    };

    private readonly Queue<(AsyncGPUReadbackRequest, NativeArray<byte>, long, RenderTexture, RenderTexture)> _videoRequests = new();
    private readonly ConcurrentStack<float[]> _audioBuffers = new();
    private readonly ConcurrentStack<NativeArray<byte>> _videoBuffers = new();

    private RecorderState _state = RecorderState.Idle;
    private int _sampleRate;
    private IntPtr _muxer = IntPtr.Zero;
    private IntPtr _encoder = IntPtr.Zero;
    private double _audioNextTime;
    private float[] _audioGap;
    private Channel<(int, float[])> _encodeAudioChannel = null;
    private double _videoStartTime;
    private Channel<(NativeArray<byte>, long)> _encodeVideoChannel = null;
    private Channel<IntPtr> _muxChannel;
    private Task _backgroundTask = Task.CompletedTask;
    private int _audioBufferBytes = 0;
    private int _videoBufferBytes = 0;

    public bool Begin(string filenameWithoutExtension)
    {
      if (_state != RecorderState.Idle)
      {
        return false;
      }

      AudioListener audioListener = GetComponent<AudioListener>();
      Camera camera = GetComponent<Camera>();

      if (!audioListener && !camera)
      {
        return false;
      }

      // TODO: enforce settings
      _sampleRate = AudioSettings.outputSampleRate;
      int channels = s_channels[AudioSettings.speakerMode];
      AudioSettings.GetDSPBufferSize(out int audioBufferSize, out _);
      if (Native.UMRBegin(
        ref _muxer,
        $"{filenameWithoutExtension}.{(camera ? "mp4" : "m4a")}",
        ref _encoder,
        (int)(audioListener ? AudioCodecID.AAC : AudioCodecID.NONE),
        audioListener ? _sampleRate : 0,
        audioListener ? AudioBitRate : 0,
        audioListener ? channels : 0,
        audioListener ? audioBufferSize : 0,
        (int)(camera ? VideoCodecID.H264 : VideoCodecID.NONE),
        camera ? camera.targetTexture ? camera.targetTexture.width : Screen.width : 0,
        camera ? camera.targetTexture ? camera.targetTexture.height : Screen.height : 0,
        camera ? VideoBitRate : 0
      ) == 0)
      {
        return false;
      }

      _state = RecorderState.Recording;

      if (audioListener)
      {
        _audioNextTime = -1;
        _audioGap = new float[channels * audioBufferSize];

        _encodeAudioChannel = Channel.CreateUnbounded<(int, float[])>(new()
        {
          SingleReader = true,
        });
      }

      if (camera)
      {
        _videoStartTime = -1;

        RenderPipelineManager.endCameraRendering += OnEndCameraRendering;

        _encodeVideoChannel = Channel.CreateUnbounded<(NativeArray<byte>, long)>(new()
        {
          SingleReader = true,
          SingleWriter = true
        });
      }

      _muxChannel = Channel.CreateUnbounded<IntPtr>(new()
      {
        SingleReader = true
      });

      _backgroundTask = Task.Run(BackgroundThreadFunction);

      return true;
    }

    public bool End()
    {
      if (_state != RecorderState.Recording)
      {
        return false;
      }

      if (_encodeAudioChannel != null)
      {
        if (!_encodeAudioChannel.Writer.TryComplete())
        {
          return false;
        }
      }

      if (_encodeVideoChannel != null)
      {
        if (_videoRequests.Count == 0)
        {
          if (!_encodeVideoChannel.Writer.TryComplete())
          {
            return false;
          }
        }

        RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;
      }

      _state = RecorderState.FinishingUp;

      return true;
    }

    private void OnAudioFilterRead(float[] data, int channels)
    {
      if (_state != RecorderState.Recording)
      {
        return;
      }

      if (_encodeAudioChannel == null)
      {
        return;
      }

      if (!TryGetAudioBuffer(out float[] buffer, data.Length))
      {
        return;
      }

      if (_audioNextTime < 0)
      {
        _audioNextTime = AudioSettings.dspTime;
      }
      double deltaTime = 1.0 * buffer.Length / channels / _sampleRate;
      int gaps = 0;
      while (_audioNextTime + deltaTime / 2 <= AudioSettings.dspTime)
      {
        gaps++;
        _audioNextTime += deltaTime;
      }
      _audioNextTime = AudioSettings.dspTime + deltaTime;

      data.CopyTo(buffer, 0);

      if (!_encodeAudioChannel.Writer.TryWrite((gaps, buffer)))
      {
        _audioBuffers.Push(buffer);
      }
    }

    private void OnDestroy()
    {
      if (_state == RecorderState.Recording)
      {
        _state = RecorderState.FinishingUp;

        if (_encodeAudioChannel != null)
        {
          _encodeAudioChannel.Writer.TryComplete();
        }

        if (_encodeVideoChannel != null)
        {
          RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

          _encodeVideoChannel.Writer.TryComplete();
        }
      }

      if (_state == RecorderState.FinishingUp)
      {
        _backgroundTask.Wait();

        while (_videoRequests.Count > 0)
        {
          (AsyncGPUReadbackRequest request, NativeArray<byte> data, long _, RenderTexture screenRT, RenderTexture vFlipRT) = _videoRequests.Dequeue();

          request.WaitForCompletion();

          data.Dispose();

          if (screenRT)
          {
            RenderTexture.ReleaseTemporary(screenRT);
          }

          if (vFlipRT)
          {
            RenderTexture.ReleaseTemporary(vFlipRT);
          }
        }
      }

      while (!_videoBuffers.IsEmpty)
      {
        if (_videoBuffers.TryPop(out NativeArray<byte> buffer))
        {
          buffer.Dispose();
        }
      }
    }

    private void OnEndCameraRendering(ScriptableRenderContext _, Camera camera)
    {
      if (camera.gameObject != gameObject)
      {
        return;
      }

      int width = camera.targetTexture ? camera.targetTexture.width : Screen.width;
      int height = camera.targetTexture ? camera.targetTexture.height : Screen.height;

      if (!TryGetVideoBuffer(out NativeArray<byte> buffer, width * height * 4))
      {
        return;
      }

      RenderTexture rt = camera.targetTexture;
      RenderTexture screenRT = null;
      RenderTexture vFlipRT = null;
      if (!rt)
      {
        screenRT = RenderTexture.GetTemporary(width, height, 0);
        ScreenCapture.CaptureScreenshotIntoRenderTexture(screenRT);
        rt = screenRT;
      }
      if (!screenRT || !SystemInfo.graphicsUVStartsAtTop)
      {
        vFlipRT = RenderTexture.GetTemporary(width, height, 0);
        Graphics.Blit(rt, vFlipRT, new Vector2(1, -1), new(0, 1));
        rt = vFlipRT;
      }

      AsyncGPUReadbackRequest request = AsyncGPUReadback.RequestIntoNativeArray(ref buffer, rt, 0, AsyncGPUReadbackRequestCallback);

      if (_videoStartTime < 0)
      {
        _videoStartTime = Time.unscaledTimeAsDouble;
      }
      long pts = Convert.ToInt64((Time.unscaledTimeAsDouble - _videoStartTime) * 1000);

      _videoRequests.Enqueue((request, buffer, pts, screenRT, vFlipRT));
    }

    private async Task BackgroundThreadFunction()
    {
      Task encodeAudioTask = _encodeAudioChannel != null ? Task.Run(EncodeAudioThreadFunction) : Task.CompletedTask;
      Task encodeVideoTask = _encodeVideoChannel != null ? Task.Run(EncodeVideoThreadFunction) : Task.CompletedTask;
      Task muxTask = Task.Run(MuxThreadFunction);

      await Task.WhenAll(encodeAudioTask, encodeVideoTask);

      _muxChannel.Writer.TryComplete();

      await muxTask;

      Native.UMREnd(ref _encoder, ref _muxer);

      _encodeAudioChannel = null;
      _encodeVideoChannel = null;

      _state = RecorderState.Idle;
    }

    private bool TryGetAudioBuffer(out float[] buffer, int length)
    {
      if (_audioBuffers.TryPop(out buffer))
      {
        return true;
      }

      if (_audioBufferBytes + length * sizeof(float) > AudioBufferByteLimit)
      {
        return false;
      }

      buffer = new float[length];

      _audioBufferBytes += length * sizeof(float);

      return true;
    }

    private bool TryGetVideoBuffer(out NativeArray<byte> buffer, int length)
    {
      if (_videoBuffers.TryPop(out buffer))
      {
        return true;
      }

      if (_videoBufferBytes + length * sizeof(byte) > VideoBufferByteLimit)
      {
        return false;
      }

      buffer = new(length, Allocator.Persistent);

      _videoBufferBytes += length * sizeof(byte);

      return true;
    }

    private void AsyncGPUReadbackRequestCallback(AsyncGPUReadbackRequest _)
    {
      while (_videoRequests.Count > 0 && _videoRequests.Peek().Item1.done)
      {
        (AsyncGPUReadbackRequest request, NativeArray<byte> data, long pts, RenderTexture screenRT, RenderTexture vFlipRT) = _videoRequests.Dequeue();

        if (!request.hasError)
        {
          if (!_encodeVideoChannel.Writer.TryWrite((data, pts)))
          {
            _videoBuffers.Push(data);
          }
        }

        if (screenRT)
        {
          RenderTexture.ReleaseTemporary(screenRT);
        }

        if (vFlipRT)
        {
          RenderTexture.ReleaseTemporary(vFlipRT);
        }
      }

      if (_state == RecorderState.FinishingUp && _videoRequests.Count == 0)
      {
        _encodeVideoChannel.Writer.TryComplete();
      }
    }

    private async Task EncodeAudioThreadFunction()
    {
      await foreach ((int gaps, float[] data) in _encodeAudioChannel.Reader.ReadAllAsync())
      {
        for (int i = 0; i < gaps; i++)
        {
          IntPtr gapPackets = Native.UMREncodeAudio(_encoder, _audioGap);

          if (gapPackets != IntPtr.Zero)
          {
            _muxChannel.Writer.TryWrite(gapPackets);
          }
        }

        IntPtr dataPackets = Native.UMREncodeAudio(_encoder, data);

        _audioBuffers.Push(data);

        if (dataPackets != IntPtr.Zero)
        {
          _muxChannel.Writer.TryWrite(dataPackets);
        }
      }
    }

    private async Task EncodeVideoThreadFunction()
    {
      await foreach ((NativeArray<byte> data, long pts) in _encodeVideoChannel.Reader.ReadAllAsync())
      {
        IntPtr packets;
        unsafe
        {
          packets = Native.UMREncodeVideo(_encoder, (IntPtr)data.GetUnsafeReadOnlyPtr(), pts);
        }

        _videoBuffers.Push(data);

        if (packets != IntPtr.Zero)
        {
          _muxChannel.Writer.TryWrite(packets);
        }
      }
    }

    private async Task MuxThreadFunction()
    {
      await foreach (IntPtr packets in _muxChannel.Reader.ReadAllAsync())
      {
        Native.UMRMux(_muxer, packets);
      }
    }
  }
}