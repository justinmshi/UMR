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
    private const int AudioBufferByteLimit = 32000000; // TODO
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
    private IntPtr _muxer = IntPtr.Zero;
    private IntPtr _encoder = IntPtr.Zero;
    private double _audioStartTime; // TODO
    private Channel<float[]> _encodeAudioChannel;
    private Task _encodeAudioTask = Task.CompletedTask;
    private double _videoStartTime;
    private Channel<(NativeArray<byte>, long)> _encodeVideoChannel;
    private Task _encodeVideoTask = Task.CompletedTask;
    private Channel<IntPtr> _muxChannel;
    private Task _muxTask = Task.CompletedTask;
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
      AudioConfiguration audioConfig = AudioSettings.GetConfiguration();
      if (Native.UMRBegin(
        ref _muxer,
        $"{filenameWithoutExtension}.{(camera ? "mp4" : "m4a")}",
        ref _encoder,
        (int)(audioListener ? AudioCodecID.AAC : AudioCodecID.NONE),
        audioListener ? audioConfig.sampleRate : 0,
        audioListener ? AudioBitRate : 0,
        audioListener ? s_channels[audioConfig.speakerMode] : 0,
        audioListener ? audioConfig.dspBufferSize : 0,
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
        _audioStartTime = -1;

        _encodeAudioChannel = Channel.CreateUnbounded<float[]>(new()
        {
          SingleReader = true,
        });
        _encodeAudioTask = Task.Run(EncodeAudioThreadFunction);
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
        _encodeVideoTask = Task.Run(EncodeVideoThreadFunction);
      }

      _muxChannel = Channel.CreateUnbounded<IntPtr>(new()
      {
        SingleReader = true
      });
      _muxTask = Task.Run(MuxThreadFunction);

      return true;
    }

    public bool End()
    {
      if (_state != RecorderState.Recording)
      {
        return false;
      }

      if (!_encodeVideoTask.IsCompleted)
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

    private void OnAudioFilterRead(float[] data, int _)
    {
      if (_encodeAudioTask.IsCompleted)
      {
        return;
      }

      if (_state == RecorderState.Recording)
      {
        if (!_audioBuffers.TryPop(out float[] buffer))
        {
          buffer = new float[data.Length];
        }

        data.CopyTo(buffer, 0);

        if (!_encodeAudioChannel.Writer.TryWrite(buffer))
        {
          _audioBuffers.Push(buffer);
        }
      }
      else
      {
        _encodeAudioChannel.Writer.TryComplete();
      }
    }

    private void OnDestroy()
    {
      if (_state == RecorderState.Recording)
      {
        _state = RecorderState.FinishingUp;

        if (!_encodeAudioTask.IsCompleted)
        {
          _encodeAudioChannel.Writer.TryComplete();
        }

        if (!_encodeVideoTask.IsCompleted)
        {
          RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

          _encodeVideoChannel.Writer.TryComplete();
        }
      }

      if (_state == RecorderState.FinishingUp)
      {
        _muxTask.Wait();

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

    private async Task EncodeAudioThreadFunction()
    {
      await foreach (float[] data in _encodeAudioChannel.Reader.ReadAllAsync())
      {
        IntPtr packets = Native.UMREncodeAudio(_encoder, data);

        _audioBuffers.Push(data);

        if (packets != IntPtr.Zero)
        {
          _muxChannel.Writer.TryWrite(packets);
        }
      }

      if (_encodeVideoTask.IsCompleted)
      {
        _muxChannel.Writer.TryComplete();
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

      if (!_videoBuffers.TryPop(out NativeArray<byte> buffer))
      {
        if (_videoBufferBytes + width * height * 4 > VideoBufferByteLimit)
        {
          return;
        }

        buffer = new(width * height * 4, Allocator.Persistent);

        _videoBufferBytes += buffer.Length;
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

      if (_videoStartTime < 0)
      {
        _videoStartTime = Time.realtimeSinceStartupAsDouble;
      }
      long pts = Convert.ToInt64((Time.realtimeSinceStartupAsDouble - _videoStartTime) * 1000);

      AsyncGPUReadbackRequest request = AsyncGPUReadback.RequestIntoNativeArray(ref buffer, rt, 0, AsyncGPUReadbackRequestCallback);

      _videoRequests.Enqueue((request, buffer, pts, screenRT, vFlipRT));
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

      if (_encodeAudioTask.IsCompleted)
      {
        _muxChannel.Writer.TryComplete();
      }
    }

    private async Task MuxThreadFunction()
    {
      await foreach (IntPtr packets in _muxChannel.Reader.ReadAllAsync())
      {
        Native.UMRMux(_muxer, packets);
      }

      Native.UMREnd(ref _encoder, ref _muxer);

      _state = RecorderState.Idle;
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
  }
}