FOV Changer - starter/template mod for the L4D2 PC Port.

mod.json declares a slider ("fov") and the console command that uses it
("l4d2_fov {fov}"). The launcher shows the slider on the Mods page and the
game runs the command through its own console while the mod is enabled.

To make your own mod: copy this folder, rename it, edit "title",
"description", "settings" and "commands" in mod.json. Loose game files
(scripts, materials, resource, ...) placed in the folder override the game's
own files, as with any mod. Full reference: docs/MODDING.md in the repository.
