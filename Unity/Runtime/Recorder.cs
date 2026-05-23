using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace UMR
{
  public class Recorder : MonoBehaviour
  {
    private const long BitRate = 8000000;

    private readonly Queue<(long, AsyncGPUReadbackRequest, RenderTexture, RenderTexture)> _requestQueue = new();

    private RecorderState _state = RecorderState.Idle;
    private int _width;
    private int _height;
    private IntPtr _encoder = IntPtr.Zero;
    private double _startTime;

    public bool Begin(string filename)
    {
      if (_state != RecorderState.Idle)
      {
        return false;
      }

      Camera camera = GetComponent<Camera>();
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

      _encoder = Native.UMREncodeBegin(filename, (int)CodecID.H264, _width, _height, BitRate);
      if (_encoder == IntPtr.Zero)
      {
        return false;
      }

      _startTime = -1;

      _state = RecorderState.Recording;

      RenderPipelineManager.endCameraRendering += OnEndCameraRendering;

      return true;
    }

    public bool End()
    {
      if (_state != RecorderState.Recording)
      {
        return false;
      }

      if (_requestQueue.Count == 0)
      {
        if (Native.UMREncodeEnd(ref _encoder) == 0)
        {
          return false;
        }

        _state = RecorderState.Idle;
      }
      else
      {
        _state = RecorderState.FinishingUp;
      }

      RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

      return true;
    }

    private void OnDestroy()
    {
      if (_state != RecorderState.Idle)
      {
        Native.UMREncodeEnd(ref _encoder);
      }

      if (_state == RecorderState.Recording)
      {
        RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;
      }
    }

    private void OnEndCameraRendering(ScriptableRenderContext context, Camera camera)
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
        Graphics.Blit(rt, vFlipRT, new Vector2(1, -1), new Vector2(0, 1));
        rt = vFlipRT;
      }

      if (_startTime < 0)
      {
        _startTime = Time.realtimeSinceStartupAsDouble;
      }
      long pts = Convert.ToInt64((Time.realtimeSinceStartupAsDouble - _startTime) * 1000);

      AsyncGPUReadbackRequest request = AsyncGPUReadback.Request(rt, 0, AsyncGPUReadbackRequestCallback);
      _requestQueue.Enqueue((pts, request, screenRT, vFlipRT));
    }

    private void AsyncGPUReadbackRequestCallback(AsyncGPUReadbackRequest _)
    {
      while (_requestQueue.Count > 0 && _requestQueue.Peek().Item2.done)
      {
        (long pts, AsyncGPUReadbackRequest request, RenderTexture screenRT, RenderTexture vFlipRT) = _requestQueue.Dequeue();

        if (!request.hasError)
        {
          byte[] data = request.GetData<Color32>().Reinterpret<byte>(4).ToArray();
          Native.UMREncodeEncode(_encoder, data, pts);
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

      if (_state == RecorderState.FinishingUp && _requestQueue.Count == 0)
      {
        Native.UMREncodeEnd(ref _encoder);

        _state = RecorderState.Idle;
      }
    }
  }
}