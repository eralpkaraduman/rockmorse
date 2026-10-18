using System.Collections.Generic;

public class Morse
{
	private JSONObject morseData;

	private List<JSONObject> letterList;

	public int gap;

	public int dot = 1;

	public int dash = 2;

	public int char_dot = 1;

	public int char_dash = 2;

	private int dataIndex_char;

	private int dataIndex_alt = 1;

	private int dataIndex_morse = 2;

	public Morse(string morseDataString)
	{
		morseData = new JSONObject(morseDataString);
		letterList = morseData.list;
	}

	public int[][] encode(string sentence)
	{
		if (sentence.Length <= 0)
		{
			return new int[0][];
		}
		string[] array = sentence.Split(' ');
		string[] array2 = new string[array.Length];
		int[][] array3 = new int[array2.Length][];
		for (int i = 0; i < array.Length; i++)
		{
			string text = array[i];
			List<int> list = new List<int>();
			foreach (char letter in text)
			{
				int[] array4 = encodeChar(letter);
				for (int k = 0; k < array4.Length; k++)
				{
					list.Add(array4[k]);
				}
				list.Add(gap);
			}
			int[] array5 = new int[list.Count - 1];
			for (int l = 0; l < list.Count - 1; l++)
			{
				array5[l] = list[l];
			}
			array3[i] = array5;
		}
		return array3;
	}

	private int[] encodeChar(char letter)
	{
		string text = letter.ToString();
		int[] result = new int[0];
		for (int i = 0; i < letterList.Count; i++)
		{
			List<JSONObject> list = letterList[i].list;
			string str = list[dataIndex_char].str;
			string str2 = list[dataIndex_alt].str;
			string str3 = list[dataIndex_morse].str;
			if (!(text == str) && !(text == str2))
			{
				continue;
			}
			result = new int[str3.Length];
			for (int j = 0; j < str3.Length; j++)
			{
				string text2 = str3[j].ToString();
				int num = gap;
				if (text2 == "-")
				{
					num = dash;
				}
				if (text2 == ".")
				{
					num = dot;
				}
				result[j] = num;
			}
			return result;
		}
		return result;
	}
}
