using UnityEngine;

public class CameraFollowBounds : MonoBehaviour
{
	public Mars mars;

	private float rightBound;

	private float leftBound;

	private float topBound;

	private float bottomBound;

	private Vector3 pos;

	public Transform target;

	private void Start()
	{
	}

	private void Update()
	{
		float orthographicSize = Camera.main.GetComponent<Camera>().orthographicSize;
		float num = orthographicSize * (float)Screen.width / (float)Screen.height;
		rightBound = mars.worldBunds.width / 2f - num;
		leftBound = 0f - rightBound;
		topBound = mars.worldBunds.height / 2f - orthographicSize;
		bottomBound = 0f - topBound;
		Vector3 position = new Vector3(target.position.x, target.position.y, transform.position.z);
		position.x = Mathf.Clamp(position.x, leftBound, rightBound);
		position.y = Mathf.Clamp(position.y, bottomBound, topBound);
		transform.position = position;
	}
}
