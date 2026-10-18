using UnityEngine;
using UnityEngine.SceneManagement;

public class RoverScreen : MonoBehaviour
{
	private GUITexture background;

	public GUIText screenGUIText;

	private float maxCharsPerLine = 31f;

	public int startingLifeSupport = 10;

	public int lifeSupport;

	private int numLineCapacity = 12;

	public LevelLoader levelLoader;

	public Mars mars;

	public Earth earth;

	private bool on;

	public GUIText visibilityIndicator;

	private void Start()
	{
		background = GetComponent<GUITexture>();
		startingLifeSupport++;
		lifeSupport = startingLifeSupport;
		printScreen();
	}

	private string printScreen()
	{
		string text = "EARTH:\n";
		text += "------\n";
		text += levelLoader.currentQuestion;
		string text2 = string.Empty;
		int num = text.Length;
		int num2 = 0;
		int num3 = 0;
		int num4 = 3;
		while (num > 0)
		{
			if ((float)num3 >= maxCharsPerLine)
			{
				text2 += "\n";
				num3 = 0;
				num4++;
			}
			text2 += text[num2];
			num2++;
			num3++;
			num--;
		}
		for (int num5 = numLineCapacity - num4; num5 > 0; num5--)
		{
			text2 += "\n";
		}
		text2 += "\n_______________________________";
		text2 += "\n";
		string text3 = string.Empty + lifeSupport;
		if (text3.Length < 2)
		{
			text3 = "0" + text3;
		}
		text2 = text2 + "\nLIFE SUPPORT LEFT:      " + text3 + " SOLS";
		string text4 = string.Empty + (int)mars.hoursUntilNextSol;
		string text5 = string.Empty + (int)(mars.secondsUntilNextSol / 60f % 60f);
		if (text4.Length < 2)
		{
			text4 = "0" + text4;
		}
		if (text5.Length < 2)
		{
			text5 = "0" + text5;
		}
		string text6 = text2;
		text2 = text6 + "\nHOURS UNTIL NEXT SOL:     " + text4 + ":" + text5;
		text2 = text2 + "\nSATELITE IMAGERY COVERAGE:  " + ((!mars.isEarthVisible) ? " NO" : "YES");
		visibilityIndicator.enabled = !mars.isEarthVisible;
		string text7 = string.Empty + Mathf.Round((1f - mars.darkness + 0.5f) * 100f);
		if (text7.Length < 2)
		{
			text4 = "0" + text7;
		}
		visibilityIndicator.text = "VISIBILITY IS TOO LOW\n" + text7 + "%, WAIT FOR SOL";
		return text2;
	}

	private void Update()
	{
		string text = printScreen();
		string text2 = text;
		screenGUIText.text = text2;
		lifeSupport = startingLifeSupport - mars.currentSol;
		if (lifeSupport <= -1)
		{
			SceneManager.LoadScene("Death");
		}
	}

	public void setOn(bool _on)
	{
		if (on != _on)
		{
			on = _on;
			background.enabled = on;
			screenGUIText.enabled = on;
			if (on)
			{
				earth.playerIsInRover();
			}
		}
	}
}
