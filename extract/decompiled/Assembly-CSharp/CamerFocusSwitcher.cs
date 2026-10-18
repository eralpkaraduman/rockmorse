using UnityEngine;

public class CamerFocusSwitcher : MonoBehaviour
{
	public LevelLoader levelLoader;

	public Transform playerTransform;

	public Transform roverForcusTarget;

	private float defaultCameraSize;

	public Vector3 positionTarget;

	public bool playerIsInRover;

	private float rightBound;

	private float leftBound;

	private float topBound;

	private float bottomBound;

	public Renderer nightosphere;

	private bool overViewForced;

	public Mars mars;

	private void Start()
	{
		positionTarget = playerTransform.position;
		defaultCameraSize = Camera.main.orthographicSize;
	}

	private void Update()
	{
		float num = 0f;
		float num2 = defaultCameraSize;
		bool decodedLettersHidden = true;
		if (Input.GetKey("m") || overViewForced)
		{
			positionTarget = levelLoader.transform.position;
			decodedLettersHidden = false;
			num = 1f;
			num2 = levelFocusCameraSize();
		}
		else if (playerIsInRover)
		{
			positionTarget = roverForcusTarget.position;
			num = 1f;
		}
		else
		{
			positionTarget = playerTransform.position;
			num = 0.2f;
		}
		levelLoader.setDecodedLettersHidden(decodedLettersHidden);
		Color color = nightosphere.material.color;
		color.a += (mars.darkness * 0.85f - color.a) / 5f;
		nightosphere.material.color = color;
		positionTarget = clampBounds(positionTarget);
		if (num >= 0f)
		{
			iTween.MoveUpdate(gameObject, positionTarget, num);
			Camera.main.orthographicSize += (num2 - Camera.main.orthographicSize) / 10f;
		}
		else
		{
			transform.position = positionTarget;
			Camera.main.orthographicSize = num2;
		}
	}

	public void forceOverview(bool forced)
	{
		overViewForced = forced;
	}

	private Vector3 clampBounds(Vector3 targetPos)
	{
		float orthographicSize = Camera.main.GetComponent<Camera>().orthographicSize;
		float num = orthographicSize * (float)Screen.width / (float)Screen.height;
		rightBound = mars.worldBunds.width / 2f - num;
		leftBound = 0f - rightBound;
		topBound = mars.worldBunds.height / 2f - orthographicSize;
		bottomBound = 0f - topBound;
		Vector3 result = new Vector3(targetPos.x, targetPos.y, transform.position.z);
		result.x = Mathf.Clamp(result.x, leftBound, rightBound);
		result.y = Mathf.Clamp(result.y, bottomBound, topBound);
		return result;
	}

	private float levelFocusCameraSize()
	{
		float orthographicSize = Camera.main.GetComponent<Camera>().orthographicSize;
		float num = orthographicSize * (float)Screen.width / (float)Screen.height;
		float a = levelLoader.sentenceSize.x / num;
		a = Mathf.Max(a, 0.9f);
		return defaultCameraSize * a;
	}
}
