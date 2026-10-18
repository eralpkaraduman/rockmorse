using UnityEngine;
using UnityEngine.SceneManagement;

public class EndGame : MonoBehaviour
{
	private float dismissDuration = 5f;

	private float timeSceneEntered;

	private void Start()
	{
		timeSceneEntered = Time.time;
	}

	private void Update()
	{
		if (Time.time >= timeSceneEntered + dismissDuration)
		{
			dismiss();
		}
		if (Input.GetKeyDown("space") || Input.GetMouseButtonDown(0))
		{
			dismiss();
		}
	}

	private void dismiss()
	{
		SceneManager.LoadScene(0);
	}
}
