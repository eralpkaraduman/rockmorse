using UnityEngine;

public class SnapToTarget : MonoBehaviour
{
	public Transform target;

	public Sprite dashTarget;

	public Sprite dotTarget;

	public bool isDash;

	private void Start()
	{
	}

	private void Update()
	{
		Vector3 position = transform.position;
		if (target != null)
		{
			isDash = target.GetComponent<Rock>().Dark;
			position.x = target.position.x;
			position.y = target.position.y;
			transform.position = position;
			GetComponent<Renderer>().enabled = true;
			((SpriteRenderer)GetComponent<Renderer>()).sprite = ((!isDash) ? dotTarget : dashTarget);
		}
		else
		{
			GetComponent<Renderer>().enabled = false;
		}
	}
}
