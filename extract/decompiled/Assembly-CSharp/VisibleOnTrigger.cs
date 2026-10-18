using UnityEngine;

public class VisibleOnTrigger : MonoBehaviour
{
	private SpriteRenderer spriteRenderer;

	private void Start()
	{
		spriteRenderer = GetComponent<SpriteRenderer>();
	}

	private void Update()
	{
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		if (o.tag == "Player" && !o.isTrigger)
		{
			Debug.Log("pl");
			spriteRenderer.enabled = true;
		}
	}

	private void OnTriggerExit2D(Collider2D o)
	{
		if (o.tag == "Player" && !o.isTrigger)
		{
			Debug.Log("pl-e");
			spriteRenderer.enabled = false;
		}
	}
}
