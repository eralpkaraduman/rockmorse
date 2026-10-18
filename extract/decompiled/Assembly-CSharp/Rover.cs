using UnityEngine;

public class Rover : MonoBehaviour
{
	public SpriteRenderer roof;

	private void Start()
	{
	}

	private void Update()
	{
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		if (o.tag == "Player" && !o.isTrigger)
		{
			Debug.Log("pl");
			roof.enabled = true;
		}
	}

	private void OnTriggerExit2D(Collider2D o)
	{
		if (o.tag == "Player" && !o.isTrigger)
		{
			Debug.Log("pl-e");
			roof.enabled = false;
		}
	}
}
