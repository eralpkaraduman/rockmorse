using UnityEngine;

public class Rock : MonoBehaviour
{
	public Sprite darkSprite;

	public Sprite lightSprite;

	public Transform rockShadowPrefab;

	private AudioSource dragSFXSource;

	public RockGenerator generator;

	public Transform shadow;

	private float sfxMinVelocity = 0.2f;

	private bool dark;

	public bool Dark
	{
		get
		{
			return dark;
		}
		set
		{
			SpriteRenderer spriteRenderer = GetComponent("SpriteRenderer") as SpriteRenderer;
			spriteRenderer.sprite = ((!value) ? lightSprite : darkSprite);
			dark = value;
		}
	}

	private void Start()
	{
		dragSFXSource = GetComponent("AudioSource") as AudioSource;
		shadow = Object.Instantiate(rockShadowPrefab, transform.position, Quaternion.identity) as Transform;
		shadow.parent = transform.parent;
	}

	private void Update()
	{
		float x = GetComponent<Rigidbody2D>().velocity.x;
		float y = GetComponent<Rigidbody2D>().velocity.y;
		if (Mathf.Abs(x) >= sfxMinVelocity || Mathf.Abs(y) >= sfxMinVelocity)
		{
			dragSFXSource.enabled = true;
			float num = x;
			if (Mathf.Abs(y) > Mathf.Abs(x))
			{
				num = 0f - y;
			}
			GetComponent<Rigidbody2D>().AddTorque((0f - num) * 13f);
		}
		else
		{
			dragSFXSource.enabled = false;
		}
		Vector3 position = transform.position;
		position.y -= 0.3f;
		position.z = -0.6f;
		shadow.position = position;
	}

	private void OnCollisionEnter2D(Collision2D o)
	{
		if (o.gameObject.tag == "Rover")
		{
			relocate();
		}
	}

	private void OnCollisionStay2D(Collision2D o)
	{
		if (o.gameObject.tag == "Rover")
		{
			relocate();
		}
	}

	private void OnTriggerStay2D(Collider2D o)
	{
		if (o.tag == "Rover")
		{
			relocate();
		}
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		if (o.tag == "Rover")
		{
			relocate();
		}
	}

	public void relocate()
	{
		transform.GetComponent<Rigidbody2D>().velocity = Vector2.zero;
		DistanceJoint2D component = transform.GetComponent<DistanceJoint2D>();
		if (component.connectedBody != null)
		{
			RockDragger component2 = component.connectedBody.transform.GetComponent<RockDragger>();
			component2.activeJoint = null;
			component.connectedBody = null;
		}
		if ((bool)shadow)
		{
			Object.Destroy(shadow.gameObject);
		}
		RockGenerator component3 = transform.parent.GetComponent<RockGenerator>();
		component3.replace(this);
	}
}
