using System.Collections.Generic;
using UnityEngine;

public class RockDragger : MonoBehaviour
{
	private Collider2D pullCandidate;

	public DistanceJoint2D activeJoint;

	public AudioSource strainSFX;

	public SnapToTarget dragTargetIndicator;

	private List<Collider2D> pullCandidates;

	private void Start()
	{
		pullCandidates = new List<Collider2D>();
	}

	private void Update()
	{
		pullCandidate = closestCandidate();
		if (Input.GetKeyDown("space") && pullCandidate != null && activeJoint == null)
		{
			startPulling();
		}
		if (Input.GetKeyUp("space") && (bool)activeJoint)
		{
			release();
		}
		if (activeJoint != null)
		{
			dragTargetIndicator.target = activeJoint.transform;
		}
		else if (pullCandidate != null)
		{
			dragTargetIndicator.target = pullCandidate.transform;
		}
		else
		{
			dragTargetIndicator.target = null;
		}
	}

	private Collider2D closestCandidate()
	{
		float num = float.PositiveInfinity;
		Collider2D result = null;
		for (int i = 0; i < pullCandidates.Count; i++)
		{
			Collider2D collider2D = pullCandidates[i];
			if (!(collider2D == null))
			{
				Vector2 a = collider2D.transform.position;
				Vector2 b = transform.position;
				float num2 = Vector2.Distance(a, b);
				if (num2 < num)
				{
					result = collider2D;
					num = num2;
				}
			}
		}
		return result;
	}

	private void startPulling()
	{
		DistanceJoint2D component = pullCandidate.GetComponent<DistanceJoint2D>();
		component.connectedBody = GetComponent<Rigidbody2D>();
		activeJoint = component;
		component.enabled = true;
		strainSFX.Play();
	}

	private void release()
	{
		activeJoint.enabled = false;
		activeJoint.connectedBody = null;
		activeJoint = null;
		GetComponent<Rigidbody2D>().velocity = Vector2.zero;
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		Rock component = o.GetComponent<Rock>();
		if (component != null)
		{
			pullCandidates.Add(o);
		}
	}

	public void releaseIfDragging(Transform rockTransform)
	{
		DistanceJoint2D component = rockTransform.GetComponent<DistanceJoint2D>();
		if (component == activeJoint)
		{
			release();
		}
	}

	private void OnTriggerExit2D(Collider2D o)
	{
		pullCandidates.Remove(o);
	}
}
