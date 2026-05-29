using System;
using UMR;
using UnityEngine;
using UnityEngine.InputSystem;

[RequireComponent(typeof(Camera))]
[RequireComponent(typeof(Recorder))]
public class Controller : MonoBehaviour
{
  [SerializeField] private bool _rt;

  private Recorder _recorder;
  private Camera _camera;

  private void Awake()
  {
    _recorder = GetComponent<Recorder>();
    _camera = GetComponent<Camera>();

    if (_rt)
    {
      _camera.targetTexture = new(1024, 1024, 32);
    }
  }

  private void OnDestroy()
  {
    if (_rt)
    {
      _camera.targetTexture.Release();
    }
  }

  private void Update()
  {
    if (Keyboard.current.bKey.wasPressedThisFrame)
    {
      Debug.Log($"UMR.Recorder ({name}_{GetInstanceID()}) begin: {(_recorder.Begin(GetFilename()) ? "success" : "failure")}");
    }
    else if (Keyboard.current.eKey.wasPressedThisFrame)
    {
      Debug.Log($"UMR.Recorder ({name}_{GetInstanceID()}) end: {(_recorder.End() ? "success" : "failure")}");
    }
  }

  private string GetFilename()
  {
    return $"{name}_{GetInstanceID()}_{Convert.ToInt32(Time.realtimeSinceStartup * 1000)}";
  }
}