using System;
using System.Runtime.CompilerServices;

namespace UnityEngine;

/// <summary>
///   <para>Class for generating random data.</para>
/// </summary>
public sealed class Random
{
	/// <summary>
	///   <para>Serializable structure used to hold the full internal state of the random number generator. See Also: Random.state.</para>
	/// </summary>
	[Serializable]
	public struct State
	{
		[SerializeField]
		private int s0;

		[SerializeField]
		private int s1;

		[SerializeField]
		private int s2;

		[SerializeField]
		private int s3;
	}

	[Obsolete("Deprecated. Use InitState() function or Random.state property instead.")]
	public static extern int seed
	{
		[MethodImpl(MethodImplOptions.InternalCall)]
		[WrapperlessIcall]
		get;
		[MethodImpl(MethodImplOptions.InternalCall)]
		[WrapperlessIcall]
		set;
	}

	/// <summary>
	///   <para>Gets/Sets the full internal state of the random number generator.</para>
	/// </summary>
	public static State state
	{
		get
		{
			INTERNAL_get_state(out var result);
			return result;
		}
		set
		{
			INTERNAL_set_state(ref value);
		}
	}

	/// <summary>
	///   <para>Returns a random number between 0.0 [inclusive] and 1.0 [inclusive] (Read Only).</para>
	/// </summary>
	public static extern float value
	{
		[MethodImpl(MethodImplOptions.InternalCall)]
		[WrapperlessIcall]
		get;
	}

	/// <summary>
	///   <para>Returns a random point inside a sphere with radius 1 (Read Only).</para>
	/// </summary>
	public static Vector3 insideUnitSphere
	{
		get
		{
			INTERNAL_get_insideUnitSphere(out var result);
			return result;
		}
	}

	/// <summary>
	///   <para>Returns a random point inside a circle with radius 1 (Read Only).</para>
	/// </summary>
	public static Vector2 insideUnitCircle
	{
		get
		{
			GetRandomUnitCircle(out var output);
			return output;
		}
	}

	/// <summary>
	///   <para>Returns a random point on the surface of a sphere with radius 1 (Read Only).</para>
	/// </summary>
	public static Vector3 onUnitSphere
	{
		get
		{
			INTERNAL_get_onUnitSphere(out var result);
			return result;
		}
	}

	/// <summary>
	///   <para>Returns a random rotation (Read Only).</para>
	/// </summary>
	public static Quaternion rotation
	{
		get
		{
			INTERNAL_get_rotation(out var result);
			return result;
		}
	}

	/// <summary>
	///   <para>Returns a random rotation with uniform distribution (Read Only).</para>
	/// </summary>
	public static Quaternion rotationUniform
	{
		get
		{
			INTERNAL_get_rotationUniform(out var result);
			return result;
		}
	}

	/// <summary>
	///   <para>Initializes the random number generator state with a seed.</para>
	/// </summary>
	/// <param name="seed">Seed used to initialize the random number generator.</param>
	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	public static extern void InitState(int seed);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void INTERNAL_get_state(out State value);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void INTERNAL_set_state(ref State value);

	/// <summary>
	///   <para>Returns a random float number between and min [inclusive] and max [inclusive] (Read Only).</para>
	/// </summary>
	/// <param name="min"></param>
	/// <param name="max"></param>
	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	public static extern float Range(float min, float max);

	/// <summary>
	///   <para>Returns a random integer number between min [inclusive] and max [exclusive] (Read Only).</para>
	/// </summary>
	/// <param name="min"></param>
	/// <param name="max"></param>
	public static int Range(int min, int max)
	{
		return RandomRangeInt(min, max);
	}

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern int RandomRangeInt(int min, int max);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void INTERNAL_get_insideUnitSphere(out Vector3 value);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void GetRandomUnitCircle(out Vector2 output);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void INTERNAL_get_onUnitSphere(out Vector3 value);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void INTERNAL_get_rotation(out Quaternion value);

	[MethodImpl(MethodImplOptions.InternalCall)]
	[WrapperlessIcall]
	private static extern void INTERNAL_get_rotationUniform(out Quaternion value);

	[Obsolete("Use Random.Range instead")]
	public static float RandomRange(float min, float max)
	{
		return Range(min, max);
	}

	[Obsolete("Use Random.Range instead")]
	public static int RandomRange(int min, int max)
	{
		return Range(min, max);
	}

	/// <summary>
	///   <para>Generates a random color from HSV and alpha ranges.</para>
	/// </summary>
	/// <param name="hueMin">Minimum hue [0..1].</param>
	/// <param name="hueMax">Maximum hue [0..1].</param>
	/// <param name="saturationMin">Minimum saturation [0..1].</param>
	/// <param name="saturationMax">Maximum saturation[0..1].</param>
	/// <param name="valueMin">Minimum value [0..1].</param>
	/// <param name="valueMax">Maximum value [0..1].</param>
	/// <param name="alphaMin">Minimum alpha [0..1].</param>
	/// <param name="alphaMax">Maximum alpha [0..1].</param>
	/// <returns>
	///   <para>A random color with HSV and alpha values in the input ranges.</para>
	/// </returns>
	public static Color ColorHSV()
	{
		return ColorHSV(0f, 1f, 0f, 1f, 0f, 1f, 1f, 1f);
	}

	/// <summary>
	///   <para>Generates a random color from HSV and alpha ranges.</para>
	/// </summary>
	/// <param name="hueMin">Minimum hue [0..1].</param>
	/// <param name="hueMax">Maximum hue [0..1].</param>
	/// <param name="saturationMin">Minimum saturation [0..1].</param>
	/// <param name="saturationMax">Maximum saturation[0..1].</param>
	/// <param name="valueMin">Minimum value [0..1].</param>
	/// <param name="valueMax">Maximum value [0..1].</param>
	/// <param name="alphaMin">Minimum alpha [0..1].</param>
	/// <param name="alphaMax">Maximum alpha [0..1].</param>
	/// <returns>
	///   <para>A random color with HSV and alpha values in the input ranges.</para>
	/// </returns>
	public static Color ColorHSV(float hueMin, float hueMax)
	{
		return ColorHSV(hueMin, hueMax, 0f, 1f, 0f, 1f, 1f, 1f);
	}

	/// <summary>
	///   <para>Generates a random color from HSV and alpha ranges.</para>
	/// </summary>
	/// <param name="hueMin">Minimum hue [0..1].</param>
	/// <param name="hueMax">Maximum hue [0..1].</param>
	/// <param name="saturationMin">Minimum saturation [0..1].</param>
	/// <param name="saturationMax">Maximum saturation[0..1].</param>
	/// <param name="valueMin">Minimum value [0..1].</param>
	/// <param name="valueMax">Maximum value [0..1].</param>
	/// <param name="alphaMin">Minimum alpha [0..1].</param>
	/// <param name="alphaMax">Maximum alpha [0..1].</param>
	/// <returns>
	///   <para>A random color with HSV and alpha values in the input ranges.</para>
	/// </returns>
	public static Color ColorHSV(float hueMin, float hueMax, float saturationMin, float saturationMax)
	{
		return ColorHSV(hueMin, hueMax, saturationMin, saturationMax, 0f, 1f, 1f, 1f);
	}

	/// <summary>
	///   <para>Generates a random color from HSV and alpha ranges.</para>
	/// </summary>
	/// <param name="hueMin">Minimum hue [0..1].</param>
	/// <param name="hueMax">Maximum hue [0..1].</param>
	/// <param name="saturationMin">Minimum saturation [0..1].</param>
	/// <param name="saturationMax">Maximum saturation[0..1].</param>
	/// <param name="valueMin">Minimum value [0..1].</param>
	/// <param name="valueMax">Maximum value [0..1].</param>
	/// <param name="alphaMin">Minimum alpha [0..1].</param>
	/// <param name="alphaMax">Maximum alpha [0..1].</param>
	/// <returns>
	///   <para>A random color with HSV and alpha values in the input ranges.</para>
	/// </returns>
	public static Color ColorHSV(float hueMin, float hueMax, float saturationMin, float saturationMax, float valueMin, float valueMax)
	{
		return ColorHSV(hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax, 1f, 1f);
	}

	/// <summary>
	///   <para>Generates a random color from HSV and alpha ranges.</para>
	/// </summary>
	/// <param name="hueMin">Minimum hue [0..1].</param>
	/// <param name="hueMax">Maximum hue [0..1].</param>
	/// <param name="saturationMin">Minimum saturation [0..1].</param>
	/// <param name="saturationMax">Maximum saturation[0..1].</param>
	/// <param name="valueMin">Minimum value [0..1].</param>
	/// <param name="valueMax">Maximum value [0..1].</param>
	/// <param name="alphaMin">Minimum alpha [0..1].</param>
	/// <param name="alphaMax">Maximum alpha [0..1].</param>
	/// <returns>
	///   <para>A random color with HSV and alpha values in the input ranges.</para>
	/// </returns>
	public static Color ColorHSV(float hueMin, float hueMax, float saturationMin, float saturationMax, float valueMin, float valueMax, float alphaMin, float alphaMax)
	{
		float h = Mathf.Lerp(hueMin, hueMax, value);
		float s = Mathf.Lerp(saturationMin, saturationMax, value);
		float v = Mathf.Lerp(valueMin, valueMax, value);
		Color result = Color.HSVToRGB(h, s, v, hdr: true);
		result.a = Mathf.Lerp(alphaMin, alphaMax, value);
		return result;
	}
}
