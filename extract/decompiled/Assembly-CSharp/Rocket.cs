using UnityEngine;
using UnityEngine.SceneManagement;

public class Rocket : MonoBehaviour
{
	private void Start()
	{
	}

	private void Update()
	{
	}

	public void playerJustGitInRocket()
	{
		SceneManager.LoadScene("Win");
	}
}
