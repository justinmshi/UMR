using UnityEngine;

public class Rotator : MonoBehaviour
{
  private void Awake()
  {
    Renderer renderer = GetComponent<Renderer>();
    if (renderer)
    {
      renderer.material.color = Color.red;
    }
  }

  private void Update()
  {
    transform.Rotate(Vector3.right * 50 * Time.deltaTime);
  }
}