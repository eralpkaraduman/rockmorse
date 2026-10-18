using System.Collections.Generic;
using UnityEngine;

public class RockGenerator : MonoBehaviour
{
	public Transform rockPrefab;

	public int numRocks = 40;

	private Rect worldBounds;

	public List<Rock> rocks;

	public Transform rockShatterAnimationPrefab;

	private void Start()
	{
		rocks = new List<Rock>();
	}

	public void Generate(Rect _worldBounds)
	{
		for (worldBounds = _worldBounds; numRocks > 0; numRocks--)
		{
			bool dark = Random.Range(0, 2) == 0;
			addRock(dark);
		}
	}

	public Rock addRock(bool dark)
	{
		Vector3 position = randomPositon();
		Transform transform = Object.Instantiate(rockPrefab, position, Quaternion.identity) as Transform;
		Rock component = transform.gameObject.GetComponent<Rock>();
		component.Dark = dark;
		transform.parent = gameObject.transform;
		component.generator = this;
		rocks.Add(component);
		return component;
	}

	public Vector3 randomPositon()
	{
		float num = worldBounds.width / 2f * 0.5f;
		float num2 = worldBounds.height / 2f * 0.5f;
		float x = Random.Range(0f - num, num);
		float y = Random.Range(0f - num2, num2);
		Vector3 result = new Vector3(x, y, transform.position.z);
		return result;
	}

	public void replace(Rock rock)
	{
		Object.Instantiate(rockShatterAnimationPrefab, rock.transform.position, Quaternion.identity);
		Object.Destroy(rock.gameObject);
		numRocks++;
		Generate(worldBounds);
	}

	private void Update()
	{
	}
}
