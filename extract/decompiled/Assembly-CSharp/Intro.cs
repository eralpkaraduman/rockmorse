using UnityEngine;
using UnityEngine.SceneManagement;

public class Intro : MonoBehaviour
{
	private float dur = 2f;

	private float introDuration;

	public Transform logo;

	private void Start()
	{
		introDuration = dur;
		logo.GetComponent<Renderer>().enabled = false;
	}

	private void Update()
	{
		introDuration -= Time.deltaTime;
		if ((double)introDuration <= (double)dur - 0.1)
		{
			logo.GetComponent<Renderer>().enabled = true;
		}
		if (introDuration <= 0f)
		{
			SceneManager.LoadScene("Title");
		}
	}
}
