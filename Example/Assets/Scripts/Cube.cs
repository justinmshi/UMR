using UnityEngine;

[RequireComponent(typeof(Renderer))]
public class Cube : MonoBehaviour
{
  private void Awake()
  {
    GetComponent<Renderer>().material.color = Color.red;
  }

  private void Update()
  {
    transform.Rotate(50 * Time.deltaTime * Vector3.right);

    transform.position = new(
      15 * Mathf.Sin(2 * Mathf.PI * Time.realtimeSinceStartup / 5),
      transform.position.y,
      transform.position.z
    );
  }
}