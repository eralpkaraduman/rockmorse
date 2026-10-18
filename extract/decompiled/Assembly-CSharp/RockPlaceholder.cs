using UnityEngine;

public class RockPlaceholder : MonoBehaviour
{
	public Sprite dashSprite;

	public Sprite dotSprite;

	public Sprite incorrectSprite;

	public Sprite correctSprite;

	private AudioSource correctAudioSource;

	private bool _isDash;

	public Rock snappedRock;

	public RockDragger rockDragger;

	private SpriteRenderer spriteRenderer;

	public RockGenerator rocks;

	private float markTimeCreated;

	private float clearTime = 0.6f;

	private void Start()
	{
		markTimeCreated = Time.time;
		correctAudioSource = GetComponent<AudioSource>();
	}

	private void Update()
	{
	}

	public void setDash(bool dash)
	{
		_isDash = dash;
		setSprite((!_isDash) ? dotSprite : dashSprite);
	}

	public bool isDash()
	{
		return _isDash;
	}

	private void setSprite(Sprite sprite)
	{
		spriteRenderer = transform.GetComponent("SpriteRenderer") as SpriteRenderer;
		spriteRenderer.sprite = sprite;
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		Rock component = o.GetComponent<Rock>();
		if (component != null)
		{
			if (Time.time < markTimeCreated + clearTime)
			{
				Debug.Log("clear time");
				component.relocate();
			}
			else if (component.Dark == _isDash)
			{
				snap(component);
				correctAudioSource.Play();
				setSprite(correctSprite);
			}
			else if (snappedRock == null)
			{
				setSprite(incorrectSprite);
			}
		}
	}

	public bool isCorrect()
	{
		return spriteRenderer.sprite == correctSprite;
	}

	private void snap(Rock r)
	{
		snappedRock = r;
	}

	private void OnTriggerExit2D(Collider2D o)
	{
		Rock component = o.GetComponent<Rock>();
		if (component != null)
		{
			if (snappedRock == component)
			{
				snappedRock = null;
				setDash(_isDash);
			}
			else if (snappedRock == null)
			{
				setDash(_isDash);
			}
		}
	}
}
