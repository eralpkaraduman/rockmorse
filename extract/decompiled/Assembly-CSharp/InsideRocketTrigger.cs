using UnityEngine;

public class InsideRocketTrigger : MonoBehaviour
{
	private void Start()
	{
	}

	private void Update()
	{
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		if (o.tag == "Player")
		{
			Debug.Log("rocket triger");
			transform.parent.GetComponent<Rocket>().playerJustGitInRocket();
		}
	}
}
