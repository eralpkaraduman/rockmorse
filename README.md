# Rockmorse

A faithful C port of Rockmorse, a 2D game made for Ludum Dare 30 in August 2014.

Credits: Super Damage, which is Eren Ozel (https://erenozel.art) and Eralp Karaduman (https://eralp.dev).

This port follows the post-compo version, at its final commit. The original Unity source and the 2016 reference builds are at https://github.com/superdamage/LD30, commit `19ad7cb`. Post-compo work starts at commit `9419fcc`.

Status: the data extraction step is done. `tools/extract.py` turns the Unity project into JSON dumps in `extract/`, C headers in `port/src/` and the used assets in `port/assets/`. `INVENTORY.md` lists everything the port must implement. No game code exists yet.
