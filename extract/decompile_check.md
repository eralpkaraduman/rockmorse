# Decompile check: 2016 Mac build vs C# source

Result: the build's script logic matches the source. No real (behaviour) differences were found in the 24 game scripts, in `TimedObjectDestructor.js`, or in the iTween and JSON code the game calls.

## Method

- Build DLLs: `LD30/Unity/Bin/Mac/RockMorse.app/Contents/Resources/Data/Managed/` (`Assembly-CSharp.dll`, `Assembly-CSharp-firstpass.dll`, `Assembly-UnityScript-firstpass.dll`).
- Decompiler: `ilspycmd` 11.1.0.9782 (ICSharpCode.Decompiler 11.1.0.9782). It runs on .NET 10.0.402 SDK, runtime Microsoft.NETCore.App 10.0.13. Both installed with Homebrew (`brew install dotnet`, then `dotnet tool install --global ilspycmd`).
- Command per DLL: `ilspycmd -p -o extract/decompiled/<assembly> <dll>`. Output is in `extract/decompiled/` (368 KB, 46 `.cs` files).
- `Assembly-UnityScript*.dll` exists only as `Assembly-UnityScript-firstpass.dll`. There is no `Assembly-UnityScript.dll`.
- Checker: `tools/decompile_check.py`. Run it with `python3 -I`. Arguments are pairs of a source directory and a decompiled directory. It does these steps.
  1. Evaluates `#if`/`#define` (JSONObject uses `PRETTY` and `USEFLOAT`, not `POOLING`).
  2. Tokenizes and drops comments, access modifiers, `this.`, `Object.`, `Debug.Log` statements (counted per method), and local declaration types.
  3. Renames locals to `L`. A second strict pass numbers them by first use, to catch wrong wiring.
  4. Canonicalizes float literals (an `f` literal is rounded to float32; an unsuffixed literal stays double), `string.Empty`, `Mathf.Infinity`, `x == false`, `0 - x`, `(float)` and `(double)` casts (counted), named arguments, `new Vector2()` vs `default`, delegate wrappers, `ref` locals, redundant parentheses, and ILSpy's inverted ternaries.
  5. Matches members by class, name and parameter count. It diffs the statement lists and prints `SAME`, `DIFF` (with `src:<line>` and `dec:<line>`), `SRC_ONLY` or `DEC_ONLY`.
- Every remaining `DIFF` was then read against the source by hand. Mutation test: changing a constant, an operator and a comparison in a copy of `Rock.cs` makes the tool report all three.

Limits. The tool drops braces, so a statement moved across a block boundary is not caught by it. I read each flagged method fully to cover that. Methods marked `SAME` match statement for statement, with locals wired the same way (strict pass), but only the tool checked them.

## Counts (24 game scripts, `Assembly-CSharp`)

275 members `SAME` (5 of them differ only by `(float)` casts that are implicit in the source). 24 members `DIFF` on first pass. All 24 were read and are cosmetic. No `SAME-LOOSE` (wiring) cases.

## Verdicts per script

All verdicts are MATCH. Cosmetic differences are listed with source and decompiled line numbers. Kinds:

- A: ILSpy inverts an `if`/early `return`/`continue`, so the same code appears as `if (!c) return;` against `if (c) {...}`.
- B: ternary written with the condition negated and the branches swapped.
- C: `while` shown as `for`, or `for` over a string shown as `foreach`.
- D: parentheses, `x == false` to `!x`, `x += 1` to `x++`, `-x` to `0f - x`, `a = b; use(a)` merged into one expression.
- E: implicit `(float)`/`(double)` conversions or `(bool)` Object casts made explicit, named arguments, `Mathf.Infinity` to `float.PositiveInfinity`, verbatim string `@"..."` to normal string.

| Script | Verdict | Cosmetic differences (src line / dec line) |
|---|---|---|
| CameraFollowBounds | MATCH | E: Update 47/26 (`Screen.width` casts), D: 50/28, 53/30 (`-rightBound`) |
| CamerFocusSwitcher | MATCH | E: clampBounds 86/84, levelFocusCameraSize 104/98 (casts), D: 89/86, 92/88 |
| CutsSceneScaleFixer | MATCH | identical |
| Earth | MATCH | A: Update 84-87/84-86, endDisplayingDecodedMessage 156/149. B: Update 114/109. D: Update 74/73, 93/90, 99/94, decode 137/132. E: fields 17/19, 31/31 (`Mathf.Infinity`), beginDisplayingDecodedMessage 151/144, endDisplayingDecodedMessage 161/153, showCutscene 192/180 (named args), beginLastLevel 219/206 (`as Transform` dropped), fields with `= false`/`= 0` defaults dropped |
| EndGame | MATCH | D: Update 17/17 |
| GUITextButton | MATCH | E: OnMouseUp 16/21, OnMouseEnter 22/30 (`(bool)` cast) |
| InsideRocketTrigger | MATCH | identical |
| Intro | MATCH | E: Update 18/21. `dur - 0.1` is double arithmetic on both sides (`(double)dur - 0.1`) |
| LevelLoader | MATCH | A: cheat 388/250, putPlaceholder 220/154, setDecodedLettersHidden 335/197. B: setDecodedLettersHidden 342/205. C/D: check 364-366/228-229, printDecodedAnswerLetters 303/173, 324/188, renderLevel 124-125/89, beginNewLine 205/145. Extra temp locals in printDecodedAnswerLetters (dec 176, 184) and setDecodedLettersHidden (dec 200, 206). Constants verified: `-1.394218f`, `0.06f`, `0.5f` |
| Mars | MATCH | D/E: fields 25/29 (`new Vector2()` vs `default`), 31/35 (`24*1.027f` = 24.647999f, float32 equal), Start 71/80, 81-86/86-92 (object initializer), Update 153/139, 161-162/144-145 (`darkness <= 0.5` is a double compare on both sides) |
| Morse | MATCH | A/C: encode 45-46/44, encodeChar 77/73 |
| NextSceneOnSpace | MATCH | D: Update 18/19 |
| Rock | MATCH | B: Dark 25/30. D: Update 67/54. E: relocate 129/108 |
| RockDragger | MATCH | D: Update 26-37/24-28 (nested ifs merged with `&&`). A: closestCandidate 57/53 |
| RockGenerator | MATCH | C: Generate 25-32/23 (`while` to `for`) |
| RockPlaceholder | MATCH | B: setDash 40/42. A/D: OnTriggerEnter2D 56-60/61-66 |
| Rocket | MATCH | identical |
| Rover | MATCH | identical |
| RoverInsideTrigger | MATCH | identical |
| RoverScreen | MATCH | D: Start 34/31. C: printScreen 68-72/59. D: 83-96/70-85. B: 93/83. A: setOn 119/108. Both sides keep the source bug `d1 = "0" + formattedDarkess` (src 97, dec 88) |
| SnapToTarget | MATCH | B: Update 33/27 |
| Title | MATCH | E: onOpenSDSite 22/22 (`@"..."` string, same URL `http://superdamage.com`) |
| TopDownControls | MATCH | B: FixedUpdate 68/58, 72/60. D: Start 29-30/29-30. E: Start cast |
| VisibleOnTrigger | MATCH | identical |
| TimedObjectDestructor.js | MATCH | See below |

### TimedObjectDestructor.js

Compiled into `Assembly-UnityScript-firstpass.dll` (it lives under Standard Assets). Source and decompiled agree: `timeOut = 1.0` (ctor sets `1f`), `detachChildren = false`, `Awake()` calls `Invoke("DestroyNow", timeOut)`, `DestroyNow()` calls `transform.DetachChildren()` if `detachChildren`, then `DestroyObject(gameObject)`. The only addition is the empty `Main()` that UnityScript always emits (`DEC_ONLY`). Scene values (`timeOut` 2.0) come from serialized data, already checked in `build_comparison.json`.

## Third-party code the game calls

### iTween: MATCH on the called path

The game calls only `iTween.MoveUpdate(GameObject, Vector3, float)` (`CamerFocusSwitcher.cs:70`). The reachable members are `MoveUpdate/3` (src 5297, dec 2957), `Hash` (6533 / 3850), `MoveUpdate/2` (5205 / 2873), `CleanArgs` (6758 / 4052), and the `Defaults` class (192-200 / 70-84).

- `MoveUpdate/3`: SAME.
- `Hash`: same; `while ... i += 2` became `for`.
- `MoveUpdate/2`: same statements, in order. Differences are cosmetic: `if/else` for `islocal` became a ternary (5222-5225 / 2888), ref-local chains for `vector3s[0] = vector3s[1] = ...`, and `iTween.LookUpdate` is now `LookUpdate`. `SmoothDamp` on x, y, z with `time = args["time"] * updateTimePercentage`, then the Rigidbody branch. Identical.
- `CleanArgs`: same (`Int32`/`Double` shown as `int`/`double`).
- `Defaults`: `updateTimePercentage = 0.05f`, `updateTime = 1f * updateTimePercentage`, `isLocal = false`. Same.

The other 60 or so iTween members (ColorTo, Rotate*, Shake*, Punch*, Stop*, Pause*, Resume*, easing functions, `Start`/`TweenDelay`/`TweenRestart` coroutines) are not called by the game. The tool marks them `DIFF`, and the remaining differences are the same lowering kinds as above. Examples are `foreach` to indexed `for` (e.g. 6345/3695), De Morgan on `if` (6572/3883), `(LoopType)(int)obj` for enum unboxing (6854/4149), `Mathf.PI` shown as `(float)Math.PI` (7252/4574), and ILSpy float fractions such as `1/2.75f` shown as `372f/1023f` (7315/4649). I scanned all of these diff lines for a changed number, operator or call and found none. The three coroutines were not compared line by line.

### JSONObject: MATCH on the called path

The game uses `new JSONObject(string)`, `.list`, `.str` and the string indexer (`LevelLoader.cs:117-124`, `Morse.cs:23, 72`). The compared and read-through members are the `JSONObject(string, int, bool, bool)` constructor, `Parse` (src 229-364, dec 294-460), `Create` overloads, `CreateStringObject`, `CreateBakedObject`, `Add`, `AddField`, `GetField(string)`, `HasField`, `SetField`, `RemoveField`, the `this[int]` and `this[string]` indexers, `operator bool`, `Merge`/`MergeRecur`, and the fields. The build compiled with `USEFLOAT` on and `POOLING` off (`n` is `float`, no finalizer, `Create()` is `new JSONObject()`).

`Parse` differs only in layout. Source nested `else if` chain with `INFINITY` constants became a `switch` on the string constants plus early `return`s, and `if(x == ':')` blocks became `&&`. The string-quote handling is unchanged: `Substring(1, Length - 2)` with no unescape, and `\\` skips one character. `Create(inner, (maxDepth < -1) ? -2 : maxDepth - 1)` is the same expression inverted.

`Print`, `Stringify*`, `BakeAsync` and `StringifyAsync` (coroutines), `Bake`, and `JSONTemplates` are not used by the game. The tool compared `Print`, `Bake`, `Merge` and `MergeRecur`; the others were not compared line by line (the tool does not parse their expression-bodied and multi-declarator forms). `JSONTemplates.TOJSON` differs only by layout.

### Other assemblies (not game logic)

- `Assembly-CSharp-firstpass.dll` holds only `MouseLook` (Standard Assets). It is not attached to anything in the game. The tool shows it identical except the default `axes = RotationAxes.MouseXAndY` (value 0, so the same default).
- `Assembly-UnityScript-firstpass.dll` also holds the unused Standard Assets character controller scripts (`CharacterMotor`, `FPSInputController`, and others). They were decompiled but not compared.
- `iTween` samples (`MoveSample`, `RotateSample`, `SampleInfo`): SAME.

## Real differences

None.

## Rerun

```
python3 -I tools/decompile_check.py \
  ../LD30/Unity/LD48-Unity/Assets/Scripts \
  extract/decompiled/Assembly-CSharp
```

The same form works for `Assets/iTween`, `Assets/JSON` and (with the `.js` file's directory) `extract/decompiled/Assembly-UnityScript-firstpass`.
