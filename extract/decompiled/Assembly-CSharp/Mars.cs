using UnityEngine;

public class Mars : MonoBehaviour
{
	public Sprite[] sprites;

	public Transform tileTemplate;

	public float tileSize = 5f;

	public int numTilesX = 4;

	public int numTilesY = 4;

	private int U;

	private int V;

	private float spriteSize;

	private float x;

	private float y;

	private float genTotal;

	private float gen;

	private Vector2 topLeft = default;

	public Rect worldBunds;

	public RockGenerator rockGenerator;

	private float SOL_IN_HOURS = 24.647999f;

	private float SOL_IN_SECONDS;

	public float timeScale = 1f;

	public float secondsSpent;

	public float secondsUntilNextSol;

	public float hoursUntilNextSol;

	public float percentTimeLeftForNextSol;

	public int currentSol;

	public float darkness;

	public bool isEarthVisible;

	public bool timeFrozen;

	private float timePastWhenFrozen;

	public RoverScreen roverScreen;

	public GUIText solCounterText;

	public Rover rover;

	private float timeOffset;

	private float timeSceneEntered;

	private float lastTimeSolCounterDisplayed;

	private float solCounterDuration = 3f;

	public Earth earth;

	private void Start()
	{
		timeSceneEntered = Time.time;
		SOL_IN_SECONDS = SOL_IN_HOURS * 60f * 60f;
		timeOffset = 0f;
		worldBunds = default;
		U = numTilesX;
		V = numTilesY;
		genTotal = U * V;
		gen = genTotal;
		spriteSize = sprites[0].bounds.size.x * tileSize;
		Vector2 vector = new Vector2
		{
			x = spriteSize * (float)(numTilesX - 1),
			y = spriteSize * (float)(numTilesY - 1)
		};
		topLeft.x = 0f - vector.x / 2f;
		topLeft.y = 0f - vector.y / 2f;
		x = topLeft.x;
		y = topLeft.y;
		worldBunds.x = x;
		worldBunds.y = y;
		worldBunds.width = vector.x + spriteSize;
		worldBunds.height = vector.y + spriteSize;
		GenerateJob();
		rockGenerator.Generate(worldBunds);
	}

	private void GenerateJob()
	{
		while (V > 0)
		{
			while (U > 0)
			{
				Vector2 vector = new Vector3(x, y, 0f);
				Transform transform = Object.Instantiate(tileTemplate, vector, Quaternion.identity) as Transform;
				gen--;
				transform.parent = gameObject.transform;
				SpriteRenderer spriteRenderer = transform.GetComponent("SpriteRenderer") as SpriteRenderer;
				int num = (int)Mathf.Round(Random.Range(0, sprites.Length));
				spriteRenderer.sprite = sprites[num];
				transform.localScale = new Vector3(tileSize, tileSize, 1f);
				x += spriteSize;
				U--;
			}
			U = numTilesX;
			x = topLeft.x;
			y += spriteSize;
			V--;
		}
	}

	private void Update()
	{
		float num = Time.time - timeSceneEntered - timePastWhenFrozen;
		int num2 = currentSol;
		if (!timeFrozen)
		{
			secondsSpent = timeOffset + num * timeScale;
		}
		else
		{
			timePastWhenFrozen += Time.deltaTime;
		}
		percentTimeLeftForNextSol = 1f - secondsSpent % SOL_IN_SECONDS / SOL_IN_SECONDS;
		secondsUntilNextSol = SOL_IN_SECONDS * percentTimeLeftForNextSol;
		hoursUntilNextSol = SOL_IN_HOURS * percentTimeLeftForNextSol;
		currentSol = (int)Mathf.Ceil(secondsSpent / SOL_IN_SECONDS);
		solCounterText.text = "SOL " + currentSol + "\n" + roverScreen.lifeSupport + " LEFT";
		darkness = Mathf.Abs((1f - percentTimeLeftForNextSol - 0.5f) / 0.5f);
		isEarthVisible = (double)darkness <= 0.5;
		if (num2 != currentSol)
		{
			newSol();
		}
		if (Time.time > lastTimeSolCounterDisplayed + solCounterDuration)
		{
			solCounterText.enabled = false;
		}
	}

	private void newSol()
	{
		solCounterText.enabled = true;
		lastTimeSolCounterDisplayed = Time.time;
		earth.newSol();
	}
}
