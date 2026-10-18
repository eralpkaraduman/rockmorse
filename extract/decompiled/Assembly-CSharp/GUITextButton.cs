using UnityEngine;

public class GUITextButton : MonoBehaviour
{
	public delegate void OnButtonMouseUpEvent();

	public string identifier;

	public Color highlightedColor = Color.red;

	public Color normalColor = Color.white;

	public AudioSource higlightSFX;

	public AudioSource clickSFX;

	public event OnButtonMouseUpEvent OnButtonMouseUp;

	private void OnMouseUp()
	{
		if ((bool)clickSFX)
		{
			clickSFX.Play();
		}
		OnButtonMouseUp();
	}

	private void OnMouseEnter()
	{
		if ((bool)higlightSFX)
		{
			higlightSFX.Play();
		}
		GetComponent<GUIText>().color = highlightedColor;
	}

	private void OnMouseExit()
	{
		GetComponent<GUIText>().color = normalColor;
	}
}
