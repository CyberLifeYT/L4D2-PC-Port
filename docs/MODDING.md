# Modding the L4D2 PC Port

Mods live in `game\mods\<Mod name>\` inside your install folder. The launcher's **Mods** page lists them. From there you can turn mods on and off and set their load order (higher in the list wins). Any settings a mod declares appear there too. Changes apply the next time you press **Launch**.

**Add starter mods** installs two examples:

| Mod | What it shows |
|---|---|
| **FOV Changer** | A settings mod: a slider in the launcher drives a command (`l4d2_fov {fov}`). Copy it to start your own. |
| **Skip Intro** | A launch-options + port-setting mod: starts the game with `-novid` (no start-up movies) and runs `l4d2_skip_press_start 1`, which presses START for you on the title screen. The game then signs in your profile and storage device as normal and opens on the main menu. |

## What a mod can contain

### 1. Game files (any mod)

A mod folder is laid out like the game's `left4dead2\` folder: `scripts\`, `materials\`, `models\`, `sound\`, `resource\`, `maps\`, `media\`... The launcher adds each enabled mod as a search path in front of the game's own, so Source finds the mod's copy of a file first. A mod can also ship its files as a `zip0.360.zip` pack at its root.

This is the Xbox 360 build of the engine, so models, textures and sounds must be in their 360 formats (`.360.mdl`, `.360.vtf`, `.360.wav`...). Plain-text files work as they are on PC: scripts, VScripts, configs, resource/UI files and the like. The launcher unpacks PC `.vpk` addons into loose files when you add them.

### 2. `mod.json` (optional): title, settings and console commands

```json
{
  "title": "FOV Changer",
  "version": "1.0",
  "author": "you",
  "description": "Shown under the mod's name on the Mods page.",
  "settings": [
    { "key": "fov", "label": "Field of view", "type": "slider",
      "min": 75, "max": 120, "step": 1, "unit": "°", "default": "90",
      "help": "Tooltip text." }
  ],
  "commands": [
    "l4d2_fov {fov}"
  ],
  "launch_options": ["-novid"]
}
```

| Field | Meaning |
|---|---|
| `title`, `version`, `author`, `description` | Shown on the Mods page. |
| `settings` | Controls shown under the mod while it is enabled. `key` is letters, digits and `_`. `type` is one of `slider` (`min`, `max`, `step`, `unit`), `toggle` (value `1`/`0`) or `choice` (`choices`: `[["value", "Label"], ...]`). `default` is a string. |
| `commands` | Console commands run while the mod is enabled. Each `{key}` is replaced by that setting's current value. Commands starting with `l4d2_` set port settings instead (see below). |
| `launch_options` | Switches added to the game's command line, e.g. `-novid`. Only `-word` switches are accepted. |

#### How commands run

When you press Launch, the launcher collects the commands of every enabled mod and writes them, separated by `;`, to `l4d2_console_commands` in `l4d2_settings.ini`. The game hands them to its own console command buffer (`Cbuf_AddText`). It then re-applies them every 10 s (`l4d2_console_interval`), because the game reloads its config and profile on map changes. Results go to `logs\console.txt` in the install folder.

Many console variables are hidden in the Xbox 360 build (development-only). Before running a command, the port finds the ConVar it names and clears that flag, so commands that only work on a PC developer build work here too.

A command whose name starts with `l4d2_` is a port setting rather than a game command. Any setting from `l4d2_settings.ini` can be changed this way while the game runs. `l4d2_fov` and `l4d2_skip_press_start` are the ones made for mods. `l4d2_skip_press_start 1` presses START every 1.5 s between 4 and 40 s after start-up, and stops as soon as you press anything. For `l4d2_fov` (50–150, 0 = game default): the game clamps `fov_desired` to 75–90, so the port instead scales the client's final view FOV, turning the default 90 into your value. It applies immediately, and scoped weapons keep their zoom relative to it.

#### Launch options

The game reads these switches at start-up. Switches found in this build:

| Switch | Effect |
|---|---|
| `-novid` | No start-up movies (Valve logo, legal screen, intro). |
| `-noattractscreen` | No PRESS START screen. **Avoid:** the storage device is then never selected, so the game asks to continue without saving. Skip Intro uses `l4d2_skip_press_start` instead. |
| `-nostartupmenu`, `-noattract` | Disable the attract-mode (idle demo) timer. |

#### Rules the launcher enforces

- One simple command per entry: no `;`, quotes, `|`, braces or line breaks after the values are filled in, at most 200 characters.
- Values come only from the mod's own settings. Sliders are clamped to `min`/`max` and rounded to `step`, toggles become `1`/`0`, and choices must be one of the listed values. A mod therefore runs only the commands it lists.

## Making your own settings mod

1. On the Mods page press **Add starter mods**, then **Open mods folder**.
2. Copy `FOV Changer`, rename the copy, and edit its `mod.json`: change `title` and `description`, add your settings, and list your commands.
3. Back on the Mods page, enable it, adjust its settings, press **Save mods** and launch.
4. If a command doesn't seem to apply, check `logs\console.txt` in the install folder. It records each command run and whether the ConVar was found.

To test commands without making a mod, add them to `l4d2_settings.ini` by hand:

```ini
l4d2_console_commands=l4d2_fov 100;viewmodel_fov 60
```

## Finding console variables

The Xbox 360 build has no in-game console. Useful client variables found in this build include:

| ConVar | Default | Notes |
|---|---|---|
| `viewmodel_fov` | `50` | Weapon model FOV. |
| `fov_desired` | `90` | The player's FOV request, clamped to 75–90. Use the port's `l4d2_fov` for wider views. |
| `joy_yawsensitivity` / `joy_pitchsensitivity` | `-1` / `1` | Controller look speed (negative yaw is normal). |

Anything you know from PC L4D2 is worth trying; `logs\console.txt` says whether the ConVar exists.
