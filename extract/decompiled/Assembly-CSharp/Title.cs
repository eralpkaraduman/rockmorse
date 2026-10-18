using UnityEngine;
using UnityEngine.SceneManagement;

public class Title : MonoBehaviour
{
	public GUITextButton sdButton;

	public GUITextButton playButton;

	private void Start()
	{
	}

	private void Awake()
	{
		sdButton.OnButtonMouseUp += onOpenSDSite;
		playButton.OnButtonMouseUp += onPlay;
	}

	private void onOpenSDSite()
	{
		Application.OpenURL("http://superdamage.com");
	}

	private void onPlay()
	{
		SceneManager.LoadScene("Story1");
	}

	private void Update()
	{
	}
}
