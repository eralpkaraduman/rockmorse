using UnityEngine;

public class CutsSceneScaleFixer : MonoBehaviour
{
	private float defaultCameraScale;

	private float defaultScale;

	private void Start()
	{
		defaultCameraScale = Camera.main.orthographicSize;
		defaultScale = transform.localScale.x;
	}

	private void LateUpdate()
	{
		float num = defaultScale * Camera.main.orthographicSize / defaultCameraScale;
		transform.localScale = new Vector3(num, num, num);
	}
}
