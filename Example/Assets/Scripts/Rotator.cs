using UnityEngine;

public class Rotator : MonoBehaviour
{
  private void Update()
  {
    transform.Rotate(Vector3.right * 50 * Time.deltaTime);
  }
}