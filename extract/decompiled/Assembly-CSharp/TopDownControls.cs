using UnityEngine;

public class TopDownControls : MonoBehaviour
{
	private float walkSpeed;

	private float sprintSpeed;

	private float curSpeed;

	private float maxSpeed;

	public Transform audioListener;

	private float sfxMinVelocity = 0.5f;

	public AudioSource footStepsSFX;

	public Animator animator;

	public Mars mars;

	private RockDragger dragger;

	private void Start()
	{
		float num = 10f;
		float num2 = 10f;
		walkSpeed = num2 + num / 5f;
		sprintSpeed = walkSpeed + walkSpeed / 2f;
		walkSpeed *= 0.6f;
	}

	private void FixedUpdate()
	{
		curSpeed = walkSpeed;
		maxSpeed = curSpeed;
		GetComponent<Rigidbody2D>().velocity = new Vector2(Mathf.Lerp(0f, Input.GetAxis("Horizontal") * curSpeed, 0.8f), Mathf.Lerp(0f, Input.GetAxis("Vertical") * curSpeed, 0.8f));
		bool flag = false;
		dragger = GetComponent("RockDragger") as RockDragger;
		if (dragger.activeJoint != null)
		{
			footStepsSFX.pitch = 0.9f;
			flag = true;
		}
		else
		{
			footStepsSFX.pitch = 1.9f;
		}
		float x = GetComponent<Rigidbody2D>().velocity.x;
		float y = GetComponent<Rigidbody2D>().velocity.y;
		if (Mathf.Abs(x) >= sfxMinVelocity || Mathf.Abs(y) >= sfxMinVelocity)
		{
			footStepsSFX.enabled = true;
			Quaternion rotation = transform.rotation;
			rotation.y = ((x <= 0f) ? 180 : 0);
			transform.rotation = rotation;
			rotation.y = ((!(x <= 0f)) ? 180 : 0);
			audioListener.transform.rotation = rotation;
			animator.Play((!flag) ? "run" : "drag");
		}
		else
		{
			footStepsSFX.enabled = false;
			animator.Play("idle");
		}
		float num = mars.worldBunds.width / 2f - 0.5f;
		float num2 = mars.worldBunds.height / 2f - 0.8f;
		Vector3 position = transform.position;
		position.x = Mathf.Clamp(position.x, 0f - num, num);
		position.y = Mathf.Clamp(position.y, 0f - num2, num2);
		transform.position = position;
	}
}
