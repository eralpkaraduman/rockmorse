using UnityEngine;
using UnityEngine.SceneManagement;

public class NextSceneOnSpace : MonoBehaviour
{
	public float dismissDuration = 5f;

	private float timeSceneEntered;

	public string nextScene;

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
		SceneManager.LoadScene(nextScene);
	}
}
