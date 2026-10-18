using System.Collections.Generic;
using UnityEngine;

public class LevelLoader : MonoBehaviour
{
	public int currentLevel = -1;

	private Morse morse;

	public TextAsset levelsAsset;

	public GUIText visibiltyIndicator;

	public TextAsset morseTextAsset;

	private List<RockPlaceholder> placeholders = new List<RockPlaceholder>();

	private float U;

	private float V;

	private Vector3 linePosStart;

	public Transform placeholderPrefab;

	public Vector2 gap = new Vector2(3f, 3f);

	public RockDragger rockDragger;

	public Vector2 sentenceSize = default;

	public RockGenerator rockGenerator;

	private int lastLevelIndex;

	public Transform letterContainer;

	public Transform letterPrefab;

	private bool _lettersHidden = true;

	private List<Transform> decodedLines = new List<Transform>();

	public string currentQuestion = string.Empty;

	private string currentAnswer = string.Empty;

	private void Start()
	{
		morse = new Morse(morseTextAsset.text);
	}

	public bool nextLevel(int forceLevel)
	{
		if (forceLevel < 0)
		{
			currentLevel++;
		}
		else
		{
			currentLevel = forceLevel;
		}
		destroyCurrentLevel();
		return renderLevel(currentLevel);
	}

	private void destroyCurrentLevel()
	{
		U = 0f;
		V = 0f;
		if (placeholders != null)
		{
			for (int i = 0; i < placeholders.Count; i++)
			{
				RockPlaceholder rockPlaceholder = placeholders[i];
				Object.Destroy(rockPlaceholder.gameObject);
			}
		}
		placeholders = new List<RockPlaceholder>();
	}

	private bool renderLevel(int levelIndex)
	{
		string text = levelsAsset.text;
		List<JSONObject> list = new JSONObject(text).list;
		lastLevelIndex = list.Count - 1;
		JSONObject jSONObject = list[levelIndex];
		string str = jSONObject["question"].str;
		string text2 = (currentAnswer = jSONObject["decoded"].str);
		string str2 = jSONObject["decoded"].str;
		Debug.Log("rendering level: " + levelIndex);
		Debug.Log("question: " + str);
		Debug.Log("decoded answer: " + text2);
		currentQuestion = str;
		string text3 = string.Empty;
		printDecodedAnswerLetters(text2);
		if (levelIndex >= lastLevelIndex)
		{
			return false;
		}
		if (str2.Length <= 0)
		{
			return true;
		}
		int[][] array = morse.encode(str2);
		beginNewSentence(array.Length);
		for (int i = 0; i < array.Length; i++)
		{
			int num = array[i].Length;
			string text4 = string.Empty;
			beginNewLine(i, num);
			for (int j = 0; j < num; j++)
			{
				int num2 = array[i][j];
				text4 += num2;
				putPlaceholder(num2);
			}
			text3 += text4;
			if (i != array.Length - 1)
			{
				text3 += "/";
			}
		}
		Debug.Log("dec:" + str2 + " enc:" + text3);
		return true;
	}

	private void beginNewSentence(int numLines)
	{
		Debug.Log("nl " + numLines);
		float num = (float)(numLines + 1) * gap.y;
		sentenceSize.y = num;
		sentenceSize.x = 0f;
		linePosStart.y = num / 2f;
		linePosStart.z = transform.position.z;
	}

	private void beginNewLine(int line, int numCodes)
	{
		U = 0f;
		V++;
		Debug.Log("nl");
		float num = gap.x * (float)numCodes;
		sentenceSize.x = Mathf.Max(sentenceSize.x, num);
		linePosStart.x = (0f - num) / 2f + gap.x / 2f;
		linePosStart.y -= gap.y;
	}

	private void putPlaceholder(int code)
	{
		Vector3 position = linePosStart;
		position.x += U * gap.x;
		U++;
		if (code != morse.gap)
		{
			Transform transform = Object.Instantiate(placeholderPrefab, position, Quaternion.identity) as Transform;
			transform.parent = gameObject.transform;
			RockPlaceholder rockPlaceholder = transform.GetComponent("RockPlaceholder") as RockPlaceholder;
			rockPlaceholder.rockDragger = rockDragger;
			rockPlaceholder.setDash(code == morse.dash);
			placeholders.Add(rockPlaceholder);
		}
	}

	private void printDecodedAnswerLetters(string decoded)
	{
		foreach (Transform decodedLine in decodedLines)
		{
			Object.Destroy(decodedLine.gameObject);
		}
		decodedLines = new List<Transform>();
		string[] array = decoded.Split(' ');
		float y = gap.y * (float)array.Length / 2f + -1.394218f;
		Vector3 position = letterContainer.transform.position;
		position.y = y;
		string[] array2 = array;
		foreach (string text in array2)
		{
			Debug.Log("loop");
			Transform transform = Object.Instantiate(letterPrefab, position, Quaternion.identity) as Transform;
			TextMesh component = transform.GetComponent<TextMesh>();
			decodedLines.Add(transform);
			component.text = string.Empty;
			string text2 = text;
			foreach (char c in text2)
			{
				Debug.Log("loop2");
				component.text = component.text + c + " ";
			}
			transform.parent = letterContainer;
			position.y -= gap.y;
		}
	}

	public void setDecodedLettersHidden(bool hidden)
	{
		if (_lettersHidden != hidden)
		{
			_lettersHidden = hidden;
			MeshRenderer[] componentsInChildren = letterContainer.GetComponentsInChildren<MeshRenderer>();
			foreach (MeshRenderer meshRenderer in componentsInChildren)
			{
				meshRenderer.enabled = !hidden;
			}
			float a = ((!check()) ? 0.06f : 0.5f);
			TextMesh[] componentsInChildren2 = letterContainer.GetComponentsInChildren<TextMesh>();
			foreach (TextMesh textMesh in componentsInChildren2)
			{
				Color color = textMesh.color;
				color.a = a;
				textMesh.color = color;
			}
		}
	}

	public bool check()
	{
		string text = string.Empty;
		if (placeholders.Count <= 0)
		{
			return false;
		}
		bool result = true;
		for (int i = 0; i < placeholders.Count; i++)
		{
			RockPlaceholder rockPlaceholder = placeholders[i];
			bool flag = rockPlaceholder.isCorrect();
			text = text + flag + ",";
			if (!rockPlaceholder.isCorrect())
			{
				result = false;
			}
		}
		return result;
	}

	private void Update()
	{
		if (Input.GetKeyDown(KeyCode.C) && Input.GetKeyDown(KeyCode.LeftShift))
		{
			cheat();
		}
	}

	private void cheat()
	{
		for (int i = 0; i < placeholders.Count; i++)
		{
			RockPlaceholder rockPlaceholder = placeholders[i];
			if (!rockPlaceholder.isCorrect())
			{
				Debug.Log("rp " + rockGenerator);
				Rock rock = rockGenerator.addRock(rockPlaceholder.isDash());
				Vector3 position = rockPlaceholder.transform.position;
				position.z = rock.transform.position.z;
				rock.transform.position = position;
			}
		}
	}
}
