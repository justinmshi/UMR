using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
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
    private const long VideoBitRate = 8000000;
    private const long AudioBitRate = 128000;
    private const int VideoBufferByteLimit = 256000000;

    private readonly Queue<(long, AsyncGPUReadbackRequest, RenderTexture, RenderTexture, NativeArray<byte>)> _requests = new(); // TODO: rename
    private readonly ConcurrentStack<NativeArray<byte>> _buffers = new(); // TODO: rename

    private RecorderState _state = RecorderState.Idle;
    private int _width;
    private int _height;
    private int _sampleRate;
    private int _channels;
    private int _audioBufferSize;
    private IntPtr _encoder = IntPtr.Zero;
    private double _startTime;
    private Channel<(long, NativeArray<byte>)> _encodeChannel; // TODO: rename this and below
    private CancellationTokenSource _encodeCTS;
    private Task _encodeTask;
    private int _videoBufferBytes = 0;

    public bool Begin(string filename)
    {
      if (_state != RecorderState.Idle)
      {
        return false;
      }

      Camera camera = GetComponent<Camera>();
      AudioListener audioListener = GetComponent<AudioListener>();

      if (!camera && !audioListener)
      {
        return false;
      }

      if (camera)
      {
        if (camera.targetTexture)
        {
          _width = camera.targetTexture.width;
          _height = camera.targetTexture.height;
        }
        else
        {
          _width = Screen.width;
          _height = Screen.height;
        }
      }
      else
      {
        _width = 0;
        _height = 0;
      }

      if (audioListener)
      {
        AudioConfiguration audioConfig = AudioSettings.GetConfiguration();
        _sampleRate = audioConfig.sampleRate;
        switch (audioConfig.speakerMode)
        {
          case AudioSpeakerMode.Mono:
            {
              _channels = 1;
              break;
            }
          case AudioSpeakerMode.Stereo:
          case AudioSpeakerMode.Prologic:
            {
              _channels = 2;
              break;
            }
          case AudioSpeakerMode.Quad:
            {
              _channels = 4;
              break;
            }
          case AudioSpeakerMode.Surround:
            {
              _channels = 5;
              break;
            }
          case AudioSpeakerMode.Mode5point1:
            {
              _channels = 6;
              break;
            }
          case AudioSpeakerMode.Mode7point1:
            {
              _channels = 8;
              break;
            }
          default:
            {
              _channels = 0;
              break;
            }
        }
        _audioBufferSize = audioConfig.dspBufferSize;
      }
      else
      {
        _sampleRate = 0;
        _channels = 0;
        _audioBufferSize = 0;
      }

      _encoder = Native.UMREncodeBegin(
        $"{filename}.{(camera ? "mp4" : "m4a")}",
        (int)(camera ? VideoCodecID.H264 : VideoCodecID.NONE),
        _width,
        _height,
        camera ? VideoBitRate : 0,
        (int)(audioListener ? AudioCodecID.AAC : AudioCodecID.NONE),
        _sampleRate,
        audioListener ? AudioBitRate : 0,
        _channels,
        _audioBufferSize
      );
      if (_encoder == IntPtr.Zero)
      {
        return false;
      }

      _startTime = -1;
      _state = RecorderState.Recording;

      if (camera)
      {
        RenderPipelineManager.endCameraRendering += OnEndCameraRendering;
      }

      _encodeChannel = Channel.CreateUnbounded<(long, NativeArray<byte>)>(new UnboundedChannelOptions
      {
        SingleReader = true,
        SingleWriter = true
      });
      _encodeCTS = new();
      _encodeTask = Task.Run(EncodeThreadFunction, _encodeCTS.Token);

      return true;
    }

    public bool End()
    {
      if (_state != RecorderState.Recording)
      {
        return false;
      }

      _state = RecorderState.FinishingUp;

      RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

      if (_requests.Count == 0)
      {
        if (!_encodeChannel.Writer.TryComplete())
        {
          return false;
        }
      }

      return true;
    }

    private void OnDestroy()
    {
      if (_state == RecorderState.Recording)
      {
        _state = RecorderState.FinishingUp;

        RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

        _encodeChannel.Writer.TryComplete();
      }

      if (_state == RecorderState.FinishingUp)
      {
        while (_requests.Count > 0)
        {
          (long _, AsyncGPUReadbackRequest request, RenderTexture screenRT, RenderTexture vFlipRT, NativeArray<byte> data) = _requests.Dequeue();

          request.WaitForCompletion();

          if (screenRT)
          {
            RenderTexture.ReleaseTemporary(screenRT);
          }
          if (vFlipRT)
          {
            RenderTexture.ReleaseTemporary(vFlipRT);
          }
          data.Dispose();
        }

        _encodeCTS.Cancel();
        _encodeTask.Wait();
      }

      while (!_buffers.IsEmpty)
      {
        if (_buffers.TryPop(out NativeArray<byte> buffer))
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

      int width;
      int height;
      bool screen;
      bool vFlip;
      if (camera.targetTexture)
      {
        width = camera.targetTexture.width;
        height = camera.targetTexture.height;
        screen = false;
        vFlip = true;
      }
      else
      {
        width = Screen.width;
        height = Screen.height;
        screen = true;
        vFlip = !SystemInfo.graphicsUVStartsAtTop;
      }

      if (width != _width || height != _height)
      {
        return;
      }

      if (!_buffers.TryPop(out NativeArray<byte> buffer))
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
      if (screen)
      {
        screenRT = RenderTexture.GetTemporary(width, height, 0);
        ScreenCapture.CaptureScreenshotIntoRenderTexture(screenRT);
        rt = screenRT;
      }
      if (vFlip)
      {
        vFlipRT = RenderTexture.GetTemporary(width, height, 0);
        Graphics.Blit(rt, vFlipRT, new Vector2(1, -1), new(0, 1));
        rt = vFlipRT;
      }

      if (_startTime < 0)
      {
        _startTime = Time.realtimeSinceStartupAsDouble;
      }
      long pts = Convert.ToInt64((Time.realtimeSinceStartupAsDouble - _startTime) * 1000);

      AsyncGPUReadbackRequest request = AsyncGPUReadback.RequestIntoNativeArray(ref buffer, rt, 0, AsyncGPUReadbackRequestCallback);

      _requests.Enqueue((pts, request, screenRT, vFlipRT, buffer));
    }

    private async Task EncodeThreadFunction()
    {
      await foreach ((long pts, NativeArray<byte> data) in _encodeChannel.Reader.ReadAllAsync())
      {
        if (!_encodeCTS.Token.IsCancellationRequested)
        {
          unsafe
          {
            Native.UMREncodeSendVideo(_encoder, (IntPtr)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(data), pts);
          }
        }

        _buffers.Push(data);
      }

      Native.UMREncodeEnd(ref _encoder); // TODO: handle if encoding audio

      _state = RecorderState.Idle;
    }

    private void AsyncGPUReadbackRequestCallback(AsyncGPUReadbackRequest _)
    {
      while (_requests.Count > 0 && _requests.Peek().Item2.done)
      {
        (long pts, AsyncGPUReadbackRequest request, RenderTexture screenRT, RenderTexture vFlipRT, NativeArray<byte> data) = _requests.Dequeue();

        if (!request.hasError)
        {
          _encodeChannel.Writer.TryWrite((pts, data));
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

      if (_state == RecorderState.FinishingUp && _requests.Count == 0)
      {
        _encodeChannel.Writer.TryComplete();
      }
    }
  }
}