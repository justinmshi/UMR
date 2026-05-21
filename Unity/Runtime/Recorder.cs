using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;

namespace UMR
{
  public class Recorder : MonoBehaviour
  {
    private static class Native
    {
      [DllImport("umr_encode", EntryPoint = "umr_encode_create")]
      public static extern IntPtr UMREncodeCreate(int width, int height);
      [DllImport("umr_encode", EntryPoint = "umr_encode_begin")]
      public static extern byte UMREncodeBegin(IntPtr encoder, string filename);
      [DllImport("umr_encode", EntryPoint = "umr_encode_encode")]
      public static extern byte UMREncodeEncode(IntPtr encoder, byte[] data, long pts);
      [DllImport("umr_encode", EntryPoint = "umr_encode_end")]
      public static extern byte UMREncodeEnd(IntPtr encoder);
      [DllImport("umr_encode", EntryPoint = "umr_encode_destroy")]
      public static extern void UMREncodeDestroy(IntPtr encoder);
    }

    private readonly Queue<(long, AsyncGPUReadbackRequest)> _requestQueue = new();

    private bool _recording = false;
    private RenderTexture _screenRT;
    private RenderTexture _vFlipRT;
    private IntPtr _encoder;
    private double _startTime;

    public bool Begin(string filename)
    {
      if (_recording)
      {
        return false;
      }

      if (_requestQueue.Count > 0)
      {
        return false;
      }

      if (Native.UMREncodeBegin(_encoder, filename) == 0)
      {
        return false;
      }

      _startTime = -1;
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
        if (Native.UMREncodeEnd(_encoder) == 0)
        {
          return false;
        }
      }

      _recording = false;

      return true;
    }

    private void Awake()
    {
      _screenRT = null;
      _vFlipRT = null;
      Camera camera = GetComponent<Camera>();
      int width = 0;
      int height = 0;
      if (camera != null)
      {
        if (camera.targetTexture != null)
        {
          width = camera.targetTexture.width;
          height = camera.targetTexture.height;
        }
        else
        {
          width = Screen.width;
          height = Screen.height;
          _screenRT = new RenderTexture(width, height, 0);
        }
        if (!SystemInfo.graphicsUVStartsAtTop || camera.targetTexture != null)
        {
          _vFlipRT = new RenderTexture(width, height, 0);
        }
      }
      _encoder = Native.UMREncodeCreate(width, height);

      RenderPipelineManager.endCameraRendering += OnEndCameraRendering;
    }

    private void OnDestroy()
    {
      RenderPipelineManager.endCameraRendering -= OnEndCameraRendering;

      if (_recording || _requestQueue.Count > 0)
      {
        Native.UMREncodeEnd(_encoder);
      }
      Native.UMREncodeDestroy(_encoder);
      if (_vFlipRT != null)
      {
        _vFlipRT.Release();
      }
      if (_screenRT != null)
      {
        _screenRT.Release();
      }
    }

    private void OnEndCameraRendering(ScriptableRenderContext context, Camera camera)
    {
      if (!_recording)
      {
        return;
      }

      if (camera.gameObject != gameObject)
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
        Native.UMREncodeEnd(_encoder);
      }
    }
  }
}