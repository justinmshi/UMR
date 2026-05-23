using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace UMR
{
  public class Recorder : MonoBehaviour
  {
    private const int CodecID = 27; // H.264
    private const long BitRate = 8000000;

    private readonly Queue<(long, AsyncGPUReadbackRequest)> _requestQueue = new();

    private IntPtr _encoder = IntPtr.Zero;
    private Camera _camera;
    private RenderTexture _vFlipRT = null;
    private RenderTexture _screenRT = null;
    private double _startTime;
    private bool _recording = false;

    public bool Begin(string filename)
    {
      if (_encoder != IntPtr.Zero)
      {
        return false;
      }

      int width = 0;
      int height = 0;
      bool vFlip = false;
      bool screen = false;
      if (_camera != null)
      {
        if (_camera.targetTexture != null)
        {
          width = _camera.targetTexture.width;
          height = _camera.targetTexture.height;
          vFlip = true;
        }
        else
        {
          width = Screen.width;
          height = Screen.height;
          vFlip = !SystemInfo.graphicsUVStartsAtTop;
          screen = true;
        }
      }

      _encoder = Native.UMREncodeBegin(filename, CodecID, width, height, BitRate);
      if (_encoder == IntPtr.Zero)
      {
        return false;
      }
      if (vFlip)
      {
        _vFlipRT = new RenderTexture(width, height, 0);
      }
      if (screen)
      {
        _screenRT = new RenderTexture(width, height, 0);
      }
      _startTime = -1;

      RenderPipelineManager.endCameraRendering += OnEndCameraRendering;

      _recording = true;

      return true;
    }

    public bool End()
    {
      if (!_recording)
      {
        return false;
      }

      if (_requestQueue.Count == 0)
      {
        if (Native.UMREncodeEnd(ref _encoder) == 0)
        {
          return false;
        }
        if (_screenRT != null)
        {
          _screenRT.Release();
        }
        if (_vFlipRT != null)
        {
          _vFlipRT.Release();
        }
      }

      RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

      _recording = false;

      return true;
    }

    private void Awake()
    {
      _camera = GetComponent<Camera>();
    }

    private void OnDestroy()
    {
      if (_encoder != IntPtr.Zero)
      {
        if (_screenRT != null)
        {
          _screenRT.Release();
        }
        if (_vFlipRT != null)
        {
          _vFlipRT.Release();
        }
        Native.UMREncodeEnd(ref _encoder);

        RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;
      }
    }

    private void OnEndCameraRendering(ScriptableRenderContext context, Camera camera)
    {
      if (camera != _camera)
      {
        return;
      }

      RenderTexture rt = camera.targetTexture;
      if (rt == null)
      {
        ScreenCapture.CaptureScreenshotIntoRenderTexture(_screenRT);
        rt = _screenRT;
      }
      if (_vFlipRT != null)
      {
        Graphics.Blit(rt, _vFlipRT, new Vector2(1, -1), new Vector2(0, 1));
        rt = _vFlipRT;
      }

      if (_startTime < 0)
      {
        _startTime = Time.realtimeSinceStartupAsDouble;
      }
      long pts = Convert.ToInt64((Time.realtimeSinceStartupAsDouble - _startTime) * 1000);

      AsyncGPUReadbackRequest request = AsyncGPUReadback.Request(rt, 0, AsyncGPUReadbackRequestCallback);

      _requestQueue.Enqueue((pts, request));
    }

    private void AsyncGPUReadbackRequestCallback(AsyncGPUReadbackRequest _)
    {
      while (_requestQueue.Count > 0 && _requestQueue.Peek().Item2.done)
      {
        (long pts, AsyncGPUReadbackRequest request) = _requestQueue.Dequeue();

        if (request.hasError)
        {
          continue;
        }

        byte[] data = request.GetData<Color32>().Reinterpret<byte>(4).ToArray();

        Native.UMREncodeEncode(_encoder, data, pts);
      }

      if (!_recording && _requestQueue.Count == 0)
      {
        if (_screenRT != null)
        {
          _screenRT.Release();
        }
        if (_vFlipRT != null)
        {
          _vFlipRT.Release();
        }
        Native.UMREncodeEnd(ref _encoder);
      }
    }
  }
}