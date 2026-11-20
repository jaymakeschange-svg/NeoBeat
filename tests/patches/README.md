# Patches Batchcut wrote

`kit.NJQ` and `keys.NJQi` were made by Batchcut, through Batchcut's own saving
code, by its `MakePatches` tool:

    BatchcutMakePatches <folder> --compact

`test_endtoend` opens them with MidiJaQ's own loading code and plays them. That
join is the one thing no other test here covers: every other test exercises
MidiJaQ alone, so a format that drifted apart between the two plugins would pass
all of them and still leave you unable to open your own patches.

`--compact` keeps six zones instead of the couple of hundred a real build makes,
so the files are small enough to live here. The sounds and the layout are
Batchcut's own either way. To make them again, build Batchcut on Linux with
`BATCHCUT_LINUX_TOOLS` and run the tool, then copy the two files here.
