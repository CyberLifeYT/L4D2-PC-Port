# Adds "Game mods/PCTest" as the first search path of the given gameinfo.xtx.
param([string]$Path)
$t = [IO.File]::ReadAllText($Path)
$i = $t.IndexOf('{', $t.IndexOf('SearchPaths')) + 1
$t = $t.Insert($i, "`r`n`t`t`tGame`t`t`t`t`"mods/PCTest`"")
[IO.File]::WriteAllText($Path, $t)
