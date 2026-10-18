using UnityEngine;

public class RoverInsideTrigger : MonoBehaviour
{
	public CamerFocusSwitcher focusSwitcher;

	public RoverScreen roverScreen;

	public AudioSource audioSource;

	public float setOnPitch = 1f;

	public float setOffPitch = 1f;

	public Mars mars;

	private void Start()
	{
	}

	private void Update()
	{
	}

	private void OnTriggerEnter2D(Collider2D o)
	{
		if (o.tag == "Player" && !focusSwitcher.playerIsInRover)
		{
			audioSource.Stop();
			audioSource.pitch = setOnPitch;
			audioSource.Play();
			focusSwitcher.playerIsInRover = true;
			roverScreen.setOn(_on: true);
			mars.timeFrozen = true;
		}
	}

	private void OnTriggerExit2D(Collider2D o)
	{
		if (o.tag == "Player" && focusSwitcher.playerIsInRover)
		{
			focusSwitcher.playerIsInRover = false;
			roverScreen.setOn(_on: false);
			audioSource.Stop();
			audioSource.pitch = setOffPitch;
			audioSource.Play();
			mars.timeFrozen = false;
		}
	}
}
