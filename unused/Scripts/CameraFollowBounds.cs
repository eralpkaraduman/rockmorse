using UnityEngine;
using System.Collections;

public class CameraFollowBounds : MonoBehaviour {

	public Mars mars;

	private float rightBound;
	private float leftBound;
	private float topBound;
	private float bottomBound;
	
	private Vector3 pos;
	public Transform target;
	//private SpriteRenderer spriteBounds;
	
	// Use this for initialization
	void Start () 
	{


		/*
		spriteBounds = GameObject.Find("1 - Background").GetComponentInChildren<SpriteRenderer>();
		target = GameObject.FindWithTag("Player").transform;
		leftBound = (float)(horzExtent - spriteBounds.sprite.bounds.size.x / 2.0f);
		rightBound = (float)(spriteBounds.sprite.bounds.size.x / 2.0f - horzExtent);
		bottomBound = (float)(vertExtent - spriteBounds.sprite.bounds.size.y / 2.0f);
		topBound = (float)(spriteBounds.sprite.bounds.size.y  / 2.0f - vertExtent);
		*/

		/*
		leftBound = - mars.worldBunds.width/2;
		topBound = mars.worldBunds.y - mars.worldBunds.height/2;
		rightBound = mars.worldBunds.x + mars.worldBunds.width/2;
		bottomBound = mars.worldBunds.y + mars.worldBunds.height/2;
		*/


	}


	
	// Update is called once per frame
	void Update () 
	{
		float vertExtent = Camera.main.GetComponent<Camera>().orthographicSize;  
		float horzExtent = vertExtent * Screen.width / Screen.height;

		rightBound = mars.worldBunds.width/2 - horzExtent;
		leftBound = -rightBound;

		topBound = mars.worldBunds.height/2 - vertExtent ;
		bottomBound = -topBound;

		var pos = new Vector3(target.position.x, target.position.y, transform.position.z);
		pos.x = Mathf.Clamp(pos.x, leftBound, rightBound);
		pos.y = Mathf.Clamp(pos.y, bottomBound, topBound);

		transform.position = pos;
	}


}
