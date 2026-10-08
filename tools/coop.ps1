<#
.SYNOPSIS
  Black Ops 1 Zombies online co-op on Windows (not part of the original game): host a game, or join one.

.DESCRIPTION
  Starts build\Release\BO1Zombies.exe with the same co-op command line the web lobby builds (web/shared/launch.ts;
  the contract is in docs/multiplayer.md). Asks for anything not given as a parameter.

  Characters: 0..3 are the map's four characters, in the order of the retail scripts (Kino: 0 Dempsey, 1 Nikolai,
  2 Takeo, 3 Richtofen). A player's client number is their character (-Character); the retail scripts give each player
  the character of their client number, so up to four players with different characters need nothing else.
  Duplicate characters and players 5-8 use client numbers 4-7: the host lists every number's character with -Chars
  (e.g. "0 1 2 3 1": client 4 plays Nikolai too) and each of those players joins with -Slot 4, -Slot 5, ...; the host
  then loads mods/coop, which gives them their character.

  Every machine needs the same game files and the same mods (-Mods, -Game): client scripts run on every machine.

  Network: the host's UDP port (default 28960) must be reachable. On a LAN, allow BO1Zombies when Windows asks (or,
  as administrator: netsh advfirewall firewall add rule name="BO1 Zombies co-op" dir=in action=allow protocol=UDP
  localport=28960). Over the internet forward UDP 28960 on the host's router to the host's PC, or use a VPN such as
  Tailscale or Hamachi and join the host's VPN address.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\coop.ps1 -Host -Name Alice -Character 0 -Players 2 -Map zombie_theater
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\coop.ps1 -Join 192.168.1.20 -Name Bob -Character 1
.EXAMPLE
  Five players, two of them Takeo: host  -Host -Players 5 -Chars "0 1 2 3 2" ... ; the second Takeo  -Join <ip> -Character 2 -Slot 4
#>
param(
    # host a game ($Host is a PowerShell automatic variable, so the switch is $HostGame; -Host works)
    [Alias('Host')][switch]$HostGame,
    # join the host at this address (IP or name; '?' asks)
    [string]$Join = '',
    [string]$Name = '',
    # 0..3
    [int]$Character = -1,
    # client number to ask for; default: the character. 4..7 for a duplicate character (agree on it with the host)
    [int]$Slot = -1,
    # host: zombie_theater, zombie_pentagon, zombie_cosmodrome, zombie_coast, zombie_temple, zombie_moon,
    # zombie_cod5_prototype, zombie_cod5_asylum, zombie_cod5_sumpf, zombie_cod5_factory
    [string]$Map = '',
    # host: how many players the game waits for before round 1 (1..8)
    [int]$Players = 0,
    # host: character of every client number, '-' for unused, e.g. "0 1 2 3 2" (default: 0 1 2 3, then 0 1 2 3 again)
    [string]$Chars = '',
    # stackable mods (fs_mods), the same on every machine, e.g. "zinfo"
    [string]$Mods = '',
    # a whole-game mod (fs_game), the same on every machine
    [string]$Game = '',
    [int]$Port = 28960,
    # host: seconds of level time to wait for missing players before round 1 (0 = forever)
    [int]$Timeout = 60,
    # anything else for the command line, e.g. "+set r_fullscreen 1"
    [string]$Extra = '',
    # fs_homepath (config, logs, dumps); default build\Release. A second copy on the same PC needs its own, e.g.
    # build\home2 (relative paths are under the repo root)
    [string]$HomeDir = '',
    # print the command line, do not start the game
    [switch]$PrintOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gameDir = Join-Path $root 'build\Release'
$exe = Join-Path $gameDir 'BO1Zombies.exe'
$knownMaps = @('zombie_theater', 'zombie_pentagon', 'zombie_cosmodrome', 'zombie_coast', 'zombie_temple', 'zombie_moon',
    'zombie_cod5_prototype', 'zombie_cod5_asylum', 'zombie_cod5_sumpf', 'zombie_cod5_factory')

function Ask([string]$prompt, [string]$default) {
    $answer = Read-Host "$prompt [$default]"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $default }
    return $answer.Trim()
}

if (-not $HostGame -and -not $Join) {
    $mode = Ask 'Host a game (h) or join one (j)?' 'h'
    if ($mode -like 'j*') { $Join = '?' } else { $HostGame = $true }
}
if ($Join -eq '?') { $Join = Ask "Host's address (IP or name)" '' }
if (-not $HostGame -and -not $Join) { Write-Output 'No host address.'; exit 1 }
if (-not $Name) { $Name = Ask 'Your name' $env:USERNAME }
if ($Character -lt 0) { $Character = [int](Ask 'Your character 0-3 (Kino: 0 Dempsey, 1 Nikolai, 2 Takeo, 3 Richtofen)' '0') }
if ($Character -lt 0 -or $Character -gt 3) { Write-Output 'Character must be 0..3.'; exit 1 }
if ($Slot -lt 0) { $Slot = $Character }
if ($Slot -gt 7) { Write-Output 'Slot must be 0..7.'; exit 1 }

# the command line splits on '+' and on spaces: one token (as web/shared/launch.ts)
$playerName = ($Name -replace '[^A-Za-z0-9_\-.]', '_')
if ($playerName.Length -gt 15) { $playerName = $playerName.Substring(0, 15) }
if (-not $playerName) { $playerName = 'player' }

$stack = @($Mods -split '[ ,]+' | Where-Object { $_ })
$hostLines = @()
if ($HostGame) {
    if (-not $Map) { $Map = Ask 'Map' 'zombie_theater' }
    if ($knownMaps -notcontains $Map) { Write-Output "Note: $Map is not one of the zombie maps this script knows ($($knownMaps -join ', '))." }
    if ($Players -le 0) { $Players = [int](Ask 'Players (1-8)' '2') }
    if ($Players -lt 1 -or $Players -gt 8) { Write-Output 'Players must be 1..8.'; exit 1 }
    if ($Chars) {
        $charList = @($Chars -split '[ ,]+' | Where-Object { $_ })
    } else {
        $count = [Math]::Max([Math]::Max($Players, 4), $Slot + 1)
        $charList = @(0..($count - 1) | ForEach-Object { [string]($_ % 4) })
    }
    if ($Slot -ge $charList.Count) { $charList += @('-') * ($Slot + 1 - $charList.Count) }
    $charList[$Slot] = [string]$Character
    foreach ($c in $charList) { if ($c -notmatch '^[0-3-]$') { Write-Output "Bad -Chars entry '$c' (0-3 or -)."; exit 1 } }
    if ($charList.Count -gt 8) { Write-Output 'At most 8 client numbers.'; exit 1 }
    # mods/coop when some client number plays another character than its own number (duplicates, numbers 4-7)
    $needsCoop = $false
    for ($i = 0; $i -lt $charList.Count; $i++) { if ($charList[$i] -ne '-' -and [int]$charList[$i] -ne $i) { $needsCoop = $true } }
    if ($needsCoop -and $stack -notcontains 'coop') { $stack = @('coop') + $stack }
    # every listed client number, and room for every player (a player without a listed number gets the first free one)
    $maxClients = [Math]::Min([Math]::Max($charList.Count, $Players), 8)
    $hostLines = @(
        "+set sv_maxclients $maxClients",
        "+set bo1_expected_players $Players",
        "+set bo1_expected_timeout $Timeout",
        "+set bo1_lobby_chars $($charList -join ' ')",
        # internet clients are capped at sv_maxRate bytes/s (5000 by default, too little for zombies); LAN ones are not
        '+set sv_maxRate 25000',
        "+map $Map")
} elseif ($Slot -ge 4 -and $stack -notcontains 'coop') {
    $stack = @('coop') + $stack # the host loads it for client numbers 4-7; same mods everywhere
}

# shared part: as web/shared/launch.ts sharedCommands()
$shared = @('+set bo1_zombies 1', '+set logfile 2', "+set net_port $Port", '+set systemlink 1')
if ($Game) { $shared += "+set fs_game mods/$Game" }
$fsMods = if ($Game) { @($Game) + $stack } else { $stack }
if ($fsMods.Count) { $shared += "+set fs_mods $($fsMods -join ' ')" }
# native only (as Play.bat, without its net_ip 127.0.0.1, which would let no other machine in: the default net_ip
# "localhost" listens on every network)
$homePath = $gameDir
if ($HomeDir) {
    $homePath = if ([System.IO.Path]::IsPathRooted($HomeDir)) { $HomeDir } else { Join-Path $root $HomeDir }
    if (-not $PrintOnly) { New-Item -ItemType Directory -Force -Path $homePath | Out-Null }
}
$native = @("+set fs_b `"$gameDir`"", "+set fs_h `"$homePath`"", '+set r_fullscreen 0', '+set r_borderless 1',
    '+set vid_xpos 0', '+set vid_ypos 0')
$me = @("+set name $playerName", "+set bo1_slot $Slot")
$parts = @($native + $shared + $me)
if ($Extra) { $parts += $Extra }
if ($HostGame) { $parts += $hostLines } else { $parts += "+connect ${Join}:$Port" }
$commandLine = $parts -join ' '

Write-Output ''
Write-Output "Command line: $commandLine"
Write-Output ''
if ($HostGame) {
    $ips = @()
    try {
        $ips = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } | ForEach-Object { $_.IPAddress })
    } catch { }
    Write-Output "Hosting $Map for $Players player(s) on UDP port $Port. Characters by client number: $($charList -join ' ')$(if ($needsCoop) { ' (mods/coop on)' })."
    if ($ips.Count) { Write-Output "Players join with one of these addresses (LAN / VPN), or your public IP with UDP $Port forwarded: $($ips -join ', ')" }
    Write-Output "They run: tools\coop.ps1 -Join <address> -Name <name> -Character <0-3>$(if ($Mods) { " -Mods `"$Mods`"" })$(if ($Game) { " -Game $Game" })"
    Write-Output "Windows Firewall: allow BO1Zombies when asked, or as administrator: netsh advfirewall firewall add rule name=`"BO1 Zombies co-op`" dir=in action=allow protocol=UDP localport=$Port"
    Write-Output "Round 1 starts when $Players player(s) are in the game, or $Timeout s after the map started$(if ($Timeout -eq 0) { ' (0: no time limit)' })."
} else {
    Write-Output "Joining $Join`:$Port as client number $Slot (character $Character). The menu loads first, then the game connects."
}
Write-Output "Logs: $homePath\main\console_mp.log and games_mp.log (with fs_game: under mods\<name>); lines starting with 'coop:'."
if ($PrintOnly) { exit 0 }
if (-not (Test-Path $exe)) { Write-Output "Game not found: $exe (build it first, see README.md)."; exit 1 }
Start-Process -FilePath $exe -ArgumentList $commandLine -WorkingDirectory $gameDir
