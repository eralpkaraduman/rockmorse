using UnityEngine;

public class Earth : MonoBehaviour
{
	public Mars mars;

	public LevelLoader levelLoader;

	public SpriteRenderer couldNotDecode;

	public SpriteRenderer couldDecode;

	public SpriteRenderer newMessage;

	public SpriteRenderer tutorial;

	public RoverScreen roverScreen;

	private float didDecodeMark = float.PositiveInfinity;

	private bool earthWillRespond;

	public float maxMessageSoundVolume = 0.4f;

	public float telemetryDelay = 3.5f;

	private bool willLoadNewLevel;

	private bool lastLevel;

	private float solMark = float.PositiveInfinity;

	private bool displayingDecodedMessage;

	private float timeMarkBeganDisplayingDecodedMessage;

	private float decodedMessageDisplayDuration = 2f;

	public AudioSource roverNewMessageSound;

	public int debugForceNextLevel = -1;

	public SpriteRenderer missionUpdateIndicator;

	public CamerFocusSwitcher focusSwitcher;

	public bool testIgnoreRoverEnterance;

	public Transform rocketPrefab;

	public Rocket rocket;

	private void Start()
	{
		Color color = missionUpdateIndicator.color;
		color.a = 0f;
		missionUpdateIndicator.color = color;
		showCutscene(tutorial);
		AudioListener.volume = 1f;
	}

	public void newSol()
	{
		solMark = Time.time;
	}

	private void Update()
	{
		float time = Time.time;
		bool flag = false;
		if (Input.GetKeyDown(KeyCode.H) || Input.GetKeyDown(KeyCode.Escape) || Input.GetKeyDown(KeyCode.P))
		{
			if (!tutorial.enabled)
			{
				showCutscene(tutorial);
			}
			else
			{
				flag = true;
			}
		}
		if (displayingDecodedMessage)
		{
			if (!(timeMarkBeganDisplayingDecodedMessage + decodedMessageDisplayDuration < time))
			{
				return;
			}
			endDisplayingDecodedMessage();
		}
		if (time > solMark + telemetryDelay && mars.isEarthVisible && !willLoadNewLevel && !earthWillRespond)
		{
			decode();
		}
		if (time > didDecodeMark + telemetryDelay && !willLoadNewLevel)
		{
			didDecodeMark = float.PositiveInfinity;
			onNewMessage();
		}
		if (Input.GetKeyDown(KeyCode.Space) || flag)
		{
			newMessage.enabled = false;
			couldDecode.enabled = false;
			newMessage.enabled = false;
			tutorial.enabled = false;
			Time.timeScale = 1f;
			AudioListener.volume = 1f;
		}
		missionUpdateIndicator.enabled = willLoadNewLevel;
		roverNewMessageSound.volume = ((!willLoadNewLevel) ? 0f : maxMessageSoundVolume);
		Color color = missionUpdateIndicator.color;
		color.a += Time.deltaTime * 1.2f;
		if (color.a >= 1f)
		{
			color.a = 0f;
		}
		missionUpdateIndicator.color = color;
	}

	private void decode()
	{
		bool flag = levelLoader.check();
		if (levelLoader.currentLevel < 0)
		{
			earthWillRespond = true;
			didDecodeMark = Time.time;
		}
		else if (flag && levelLoader.currentLevel < 0)
		{
			earthWillRespond = true;
			didDecodeMark = Time.time;
		}
		else if (flag)
		{
			didDecodeMark = Time.time;
			earthWillRespond = true;
			beginDisplayingDecodedMessage();
		}
	}

	private void beginDisplayingDecodedMessage()
	{
		displayingDecodedMessage = true;
		timeMarkBeganDisplayingDecodedMessage = Time.time;
		focusSwitcher.forceOverview(forced: true);
	}

	private void endDisplayingDecodedMessage()
	{
		if (!(timeMarkBeganDisplayingDecodedMessage <= 0f))
		{
			Debug.Log("endDisplayingDecodedMessage");
			timeMarkBeganDisplayingDecodedMessage = 0f;
			focusSwitcher.forceOverview(forced: false);
			didDecodeMark = Time.time;
			showCutscene(couldDecode);
		}
	}

	public void continueAfterDecodedDisplay()
	{
		displayingDecodedMessage = false;
	}

	private void onNewMessage()
	{
		showCutscene(newMessage);
		willLoadNewLevel = true;
		earthWillRespond = false;
	}

	private void showCutscene(SpriteRenderer newMessage)
	{
		newMessage.enabled = false;
		couldDecode.enabled = false;
		newMessage.enabled = false;
		tutorial.enabled = false;
		newMessage.enabled = true;
		Time.timeScale = 0f;
		AudioListener.volume = 0f;
		roverScreen.setOn(_on: false);
	}

	public void playerIsInRover()
	{
		if (willLoadNewLevel)
		{
			bool flag = levelLoader.nextLevel(debugForceNextLevel);
			debugForceNextLevel = -1;
			lastLevel = !flag;
			Debug.Log("LL " + lastLevel);
			if (lastLevel)
			{
				beginLastLevel();
			}
			willLoadNewLevel = false;
			Debug.Log("in rover cmp");
		}
		else
		{
			Debug.Log("ELSE");
		}
	}

	public void beginLastLevel()
	{
		Transform transform = Object.Instantiate(rocketPrefab);
		rocket = transform.GetComponent<Rocket>();
	}
}
