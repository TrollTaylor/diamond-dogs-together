# Local-coop layout, owner and graphics regressions. No MGSV launch and no window.
# The hardlink case starts a hidden copy of powershell.exe. Run under Windows
# PowerShell 5.1 and PowerShell 7.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$modulePath=Join-Path $root 'tools\friend\LocalCoopWindows.psm1'
Import-Module $modulePath -Force -DisableNameChecking
$script:checks=0
function Check([bool]$Value,[string]$Message) {
    $script:checks++
    if(!$Value) {throw "Assertion failed: $Message"}
}
function Equal($Actual,$Expected,[string]$Message) {
    $script:checks++
    if($Actual -cne $Expected) {throw "Assertion failed: $Message (expected $Expected, got $Actual)"}
}
function Throws([scriptblock]$Action,[string]$Needle,[string]$Message) {
    $script:checks++;$error_= $null
    try {& $Action | Out-Null} catch {$error_=$_}
    if(!$error_ -or !$error_.Exception.Message.Contains($Needle)) {throw "Assertion failed: $Message"}
}

$areas=@(
    @{X=0;Y=0;Width=1920;Height=1040;Dpi=96},
    @{X=-2560;Y=-200;Width=2560;Height=1400;Dpi=144},
    @{X=1920;Y=40;Width=1366;Height=728;Dpi=120},
    @{X=40;Y=0;Width=1880;Height=1000;Dpi=96},
    @{X=-800;Y=100;Width=800;Height=1400;Dpi=192},
    @{X=0;Y=-1080;Width=1080;Height=1880;Dpi=144},
    @{X=0;Y=0;Width=3440;Height=1400;Dpi=96},
    @{X=1;Y=2;Width=1280;Height=1100;Dpi=96},
    @{X=-32768;Y=-32768;Width=4096;Height=4096;Dpi=288}
)
foreach($area in $areas) {
    $layout=Get-LocalCoopLayout -WorkingArea $area
    Equal $layout.Dpi $area.Dpi 'uses monitor DPI'
    Check ($layout.Host.X+$layout.Host.Width -le $layout.Guest.X -or $layout.Host.Y+$layout.Host.Height -le $layout.Guest.Y) 'windows do not overlap'
    if($layout.Orientation -eq 'side-by-side') {Check ($layout.Guest.X-($layout.Host.X+$layout.Host.Width) -eq $layout.Gap) 'horizontal gap'} else {Check ($layout.Guest.Y-($layout.Host.Y+$layout.Host.Height) -eq $layout.Gap) 'vertical gap'}
    Check ([Math]::Abs($layout.Host.Width-$layout.Guest.Width) -le 1) 'odd widths shared fairly'
    foreach($rect in $layout.Host,$layout.Guest) {
        Check ($rect.Width -ge 1 -and $rect.Height -ge 1) 'positive outer dimensions'
        Check ($rect.X -ge $area.X -and $rect.Y -ge $area.Y) 'inside working area top/left'
        Check ($rect.X+$rect.Width -le $area.X+$area.Width) 'inside working area right'
        Check ($rect.Y+$rect.Height -le $area.Y+$area.Height) 'inside working area bottom'
        Check ($rect.ClientWidth -ge 640 -and $rect.ClientHeight -ge 480) 'verified minimum client dimensions'
    }
}
Throws {Get-LocalCoopLayout -WorkingArea @{X=0;Y=0;Width=800;Height=600}} 'too small' 'too-small monitor refuses'
$alias=Get-LocalCoopLayout -WorkingArea @{Left=-1920;Top=40;Width=1920;Height=1000} -Gap 0
Equal $alias.Host.X -1920 'Left/Top rectangle supported'
Equal ($alias.Host.X+$alias.Host.Width) $alias.Guest.X 'zero gap'
Throws {Get-LocalCoopLayout -WorkingArea @{X=0;Y=0;Width=1;Height=1}} 'invalid rectangle' 'cannot fit two positive rectangles'
Throws {Get-LocalCoopLayout -WorkingArea @{X=2147483647;Y=0;Width=2;Height=1}} 'invalid rectangle' 'coordinate overflow refused'
Throws {Get-LocalCoopLayout -WorkingArea @{X=0;Y=0;Width=900;Height=600;Dpi=900}} 'DPI is invalid' 'invalid observed DPI refused'

$owner=@{ProcessId=1234;StartUtc='2026-10-10T00:00:00.1234567Z';ExecutablePath='D:\Owned Game\mgsvtpp.exe'}
$snapshot=@{ProcessId=1234;StartUtc=$owner.StartUtc;ExecutablePath='d:\owned game\mgsvtpp.exe'}
Check (Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot 'D:\Owned Game' -Snapshot $snapshot) 'matching exact owner'
foreach($bad in @(
    @{ProcessId=1235;StartUtc=$owner.StartUtc;ExecutablePath=$owner.ExecutablePath},
    @{ProcessId=1234;StartUtc='2026-10-10T00:00:00.1234568Z';ExecutablePath=$owner.ExecutablePath},
    @{ProcessId=1234;StartUtc=$owner.StartUtc;ExecutablePath='D:\Owned Game_guest\mgsvtpp.exe'},
    @{ProcessId=1234;StartUtc='';ExecutablePath=$owner.ExecutablePath},
    @{ProcessId=0;StartUtc=$owner.StartUtc;ExecutablePath=$owner.ExecutablePath}
)) {
    Check (!(Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot 'D:\Owned Game' -Snapshot $bad)) 'PID reuse/path/creation time refused'
}
Check (!(Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot 'D:\Other Game' -Snapshot $snapshot)) 'separate root refused'
$foreign=@{ProcessId=1234;StartUtc=$owner.StartUtc;ExecutablePath='D:\Other Game\mgsvtpp.exe'}
Check (!(Test-LocalCoopOwnerSnapshot -Owner $foreign -GameRoot 'D:\Owned Game' -Snapshot $snapshot)) 'claimed owner path must equal root'
$extended=@{ProcessId=1234;StartUtc=$owner.StartUtc;ExecutablePath='\\?\D:\Owned Game\mgsvtpp.exe'}
Check (Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot 'D:\Owned Game' -Snapshot $extended) 'extended path of this copy belongs'
$emptyImage=@{ProcessId=1234;StartUtc=$owner.StartUtc;ExecutablePath=''}
Check (!(Test-LocalCoopOwnerSnapshot -Owner $owner -GameRoot 'D:\Owned Game' -Snapshot $emptyImage)) 'empty image path refused'

# Guest exe hardlinked to the host exe. Windows PowerShell's Process.Path can
# be empty on the first read; the image query still names the launched copy.
# The other directory's path is the same file and must not pass.
$linkRoot=Join-Path ([IO.Path]::GetTempPath()) ('mgs-coop-owner-' + [Guid]::NewGuid().ToString('N'))
$linkHost=Join-Path $linkRoot 'host'
$linkGuest=Join-Path $linkRoot 'guest'
try {
    $null=New-Item -ItemType Directory -Path $linkHost,$linkGuest
    $hostExe=Join-Path $linkHost 'mgsvtpp.exe'
    $guestExe=Join-Path $linkGuest 'mgsvtpp.exe'
    $sourceExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    Copy-Item -LiteralPath $sourceExe -Destination $hostExe
    & cmd.exe /c "mklink /H `"$guestExe`" `"$hostExe`"" | Out-Null
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $guestExe)) { throw 'hardlink fixture was not created' }
    Initialize-LocalCoopProcessImage
    $hostId=[MgsCoopLocal.ProcessImage]::FileId($hostExe)
    $guestId=[MgsCoopLocal.ProcessImage]::FileId($guestExe)
    Check ($hostId -and $hostId -eq $guestId -and $hostId.EndsWith(':2')) 'guest fixture is a hardlink of the host file'
    Check (!(Test-LocalCoopImageBelongsToGame -ImagePath $hostExe -GameRoot $linkGuest)) 'same-file host path is not the guest copy'
    Check (Test-LocalCoopImageBelongsToGame -ImagePath $guestExe -GameRoot $linkGuest) 'guest path belongs to the guest copy'
    Check (!(Select-LocalCoopOwnedImagePath -Candidates @($hostExe) -GameRoot $linkGuest)) 'host hardlink cannot stand in for the guest'
    Check (!(Select-LocalCoopOwnedImagePath -Candidates @('', (Join-Path $linkGuest 'steam_api64.dll')) -GameRoot $linkGuest)) 'empty path and another guest file are refused'
    $guestFull=ConvertTo-LocalCoopComparablePath $guestExe
    $selected=Select-LocalCoopOwnedImagePath -Candidates @('', $hostExe, (Join-Path $linkGuest 'steam_api64.dll'), ('\\?\' + $guestFull)) -GameRoot $linkGuest
    Check ($selected -and $selected.Equals($guestFull, [StringComparison]::OrdinalIgnoreCase)) 'empty Process.Path still accepts the launched guest image'
    $linkOwner=@{ProcessId=50;StartUtc=$owner.StartUtc;ExecutablePath=$guestExe}
    $linkSnapshot=@{ProcessId=50;StartUtc=$owner.StartUtc;ExecutablePath=$selected}
    Check (Test-LocalCoopOwnerSnapshot -Owner $linkOwner -GameRoot $linkGuest -Snapshot $linkSnapshot) 'launched guest identity matches this copy'
    $otherProcess=@{ProcessId=51;StartUtc=$owner.StartUtc;ExecutablePath=$selected}
    Check (!(Test-LocalCoopOwnerSnapshot -Owner $linkOwner -GameRoot $linkGuest -Snapshot $otherProcess)) 'different process id refused for the guest copy'
    $otherCopy=@{ProcessId=50;StartUtc=$owner.StartUtc;ExecutablePath=$hostExe}
    Check (!(Test-LocalCoopOwnerSnapshot -Owner $linkOwner -GameRoot $linkGuest -Snapshot $otherCopy)) 'same pid still refuses the other hardlink path'
    $shortName=[MgsCoopLocal.ProcessImage]::ToShortPath($guestExe)
    if ($shortName -and -not $shortName.Equals($guestFull, [StringComparison]::OrdinalIgnoreCase)) {
        Check (Test-LocalCoopImageBelongsToGame -ImagePath $shortName -GameRoot $linkGuest) 'short guest path belongs to this copy'
        $shortHost=[MgsCoopLocal.ProcessImage]::ToShortPath($hostExe)
        Check ($shortHost -and !(Test-LocalCoopImageBelongsToGame -ImagePath $shortHost -GameRoot $linkGuest)) 'short host path is not the guest copy'
    }
    # No window: this is powershell.exe copied under the guest name, not MGSV.
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$guestExe
    $psi.Arguments='-NoProfile -Command Start-Sleep -Seconds 20'
    $psi.WorkingDirectory=$linkGuest
    $psi.UseShellExecute=$false
    $psi.CreateNoWindow=$true
    $proc=[Diagnostics.Process]::Start($psi)
    try {
        $read=Get-LocalCoopOwnedImagePath -Process $proc -GameRoot $linkGuest -Attempts 20
        Check ($read -and $read.Equals($guestFull, [StringComparison]::OrdinalIgnoreCase)) 'started hardlink image is the guest copy'
        $started=@{ProcessId=$proc.Id;StartUtc=$proc.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$guestExe}
        $live=@{ProcessId=$proc.Id;StartUtc=$started.StartUtc;ExecutablePath=$read}
        Check (Test-LocalCoopOwnerSnapshot -Owner $started -GameRoot $linkGuest -Snapshot $live) 'started guest process matches this copy'
        $hostLive=@{ProcessId=$proc.Id;StartUtc=$started.StartUtc;ExecutablePath=$hostExe}
        Check (!(Test-LocalCoopOwnerSnapshot -Owner $started -GameRoot $linkGuest -Snapshot $hostLive)) 'started process still refuses the host path'
    } finally {
        if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        if ($proc) { $null=$proc.WaitForExit(5000) }
    }
} finally {
    if (Test-Path -LiteralPath $linkRoot) { Remove-Item -LiteralPath $linkRoot -Recurse -Force }
}

$now=[DateTime]::Parse('2026-10-10T00:00:00Z').ToUniversalTime()
$target=(Get-LocalCoopLayout -WorkingArea $areas[0]).Host
$original=[pscustomobject]@{X=50;Y=50;Width=1280;Height=720}
$d=Get-LocalCoopPlacementDecision -Handle 99 -Rectangle $original -Target $target -NowUtc $now
Check (!$d.Place -and $d.Reason -eq 'window_settling') 'wait for rendered-window settling'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(299)
Check (!$d.Place) 'no placement before debounce'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(300)
Check ($d.Place -and $d.Reason -eq 'initial') 'one initial placement'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $target -Target $target -NowUtc $now.AddMilliseconds(600)
Check (!$d.Place) 'at target no repeated placement'
$drag=[pscustomobject]@{X=130;Y=160;Width=$target.Width;Height=$target.Height}
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $drag -Target $target -NowUtc $now.AddMilliseconds(900)
Check (!$d.Place) 'position-only user drag respected'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(1100)
Check (!$d.Place -and $d.Reason -eq 'reset_debounce') 'size reset is debounced'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(1700)
Check ($d.Place -and $d.Reason -eq 'startup_reset' -and $d.Entry.Corrections -eq 1) 'first stable startup reset corrected'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(2600)
Check ($d.Place -and $d.Entry.Corrections -eq 2) 'second bounded startup reset correction'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(3500)
Check (!$d.Place) 'no infinite resize forcing'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(8000)
Check (!$d.Place) 'settling window deadline expires'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 99 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(9000) -Retile
Check ($d.Place -and $d.Reason -eq 'retile') 'explicit user retile'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 100 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(10000)
Check (!$d.Place -and $d.Entry.Corrections -eq 0) 'recreated handle gets new bounded state'
$d=Get-LocalCoopPlacementDecision -Entry $d.Entry -Handle 100 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(10300)
Check ($d.Place -and $d.Reason -eq 'initial') 'recreated window initially placed'

# Same-size early recentering can also stack the windows. A user drag must
# take precedence and permanently disable automatic correction on that handle.
$recenter=[pscustomobject]@{X=120;Y=140;Width=$target.Width;Height=$target.Height}
$e=Get-LocalCoopPlacementDecision -Handle 201 -Rectangle $target -Target $target -NowUtc $now -Retile
$e=Get-LocalCoopPlacementDecision -Entry $e.Entry -Handle 201 -Rectangle $recenter -Target $target -NowUtc $now.AddMilliseconds(1000) -Overlapping
Check (!$e.Place) 'position reset first stabilizes'
$e=Get-LocalCoopPlacementDecision -Entry $e.Entry -Handle 201 -Rectangle $recenter -Target $target -NowUtc $now.AddMilliseconds(1600) -Overlapping
Check ($e.Place -and $e.Reason -eq 'startup_reset') 'overlapping startup position reset corrected'
$e=Get-LocalCoopPlacementDecision -Entry $e.Entry -Handle 201 -Rectangle $recenter -Target $target -NowUtc $now.AddMilliseconds(2000) -Moving -Overlapping
Check (!$e.Place -and $e.Reason -eq 'user_moving') 'active user drag never interrupted'
$e=Get-LocalCoopPlacementDecision -Entry $e.Entry -Handle 201 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(3100) -Overlapping
Check (!$e.Place -and $e.Reason -eq 'user_arranged') 'finished user arrangement persists even if overlapping'
$e=Get-LocalCoopPlacementDecision -Entry $e.Entry -Handle 201 -Rectangle $original -Target $target -NowUtc $now.AddMilliseconds(3500) -Retile
Check ($e.Place -and !$e.Entry.UserArranged) 'only explicit retile reenables auto settling'
# Graphics fixture copied from existing audited lab_scripts_tests; no save I/O.
    $realConfig = @'
{
   "as" : "UzF+nvqVP+4=",
   "graphics" : {
      "quality_setting" : {
         "depth_of_field" : "Enable",
         "effect" : "ExtraHigh",
         "lighting" : "ExtraHigh",
         "model_detail" : "ExtraHigh",
         "motion_blur_amount" : "Large",
         "postprocess" : "ExtraHigh",
         "shadow" : "ExtraHigh",
         "ssao" : "ExtraHigh",
         "texture" : "ExtraHigh",
         "texture_filtering" : "ExtraHigh",
         "volumetric_clouds" : "On"
      },
      "version" : 1,
      "videoout_setting" : {
         "display_index" : 0,
         "framerate_control" : "Auto",
         "height" : 1440,
         "vsync" : "Enable",
         "width" : 2560,
         "window_mode" : "FullScreenWindowed"
      }
   },
   "project" : "tpp"
}
'@.Replace("`r`n", "`n")

$before=Get-LocalCoopGraphicsInfo -Text $realConfig
Check $before.Valid 'known real graphics tag verifies'
Equal $before.ComputedTag 'UzF+nvqVP+4=' 'same real tag'
$edited=Set-LocalCoopGraphicsWindowedText -Text $realConfig -Width 1280 -Height 720
$after=Get-LocalCoopGraphicsInfo -Text $edited
Check $after.Valid 'windowed config tag verifies'
Equal $after.StoredTag 'a8ID1auvq+8=' 'same independently checked edited tag'
Equal ($after.WindowMode+' '+$after.Width+'x'+$after.Height) 'Windowed 1280x720' 'windowed dimensions'
Check ($edited.Contains('"lighting" : "ExtraHigh"')) 'unrelated graphics settings preserved'
Throws {Set-LocalCoopGraphicsWindowedText -Text $realConfig.Replace('"width" : 2560','"width" : 2561')} 'does not verify' 'invalid input integrity refused'
Throws {Set-LocalCoopGraphicsWindowedText -Text $realConfig -Width 319 -Height 240} 'Unsupported window size' 'unqualified render size refused'
$synthetic='{"b":1,"a":{"z":"q\"\\\n","y":-5},"c":true}' | ConvertFrom-Json
Equal (Get-LocalCoopGraphicsTag -Object $synthetic) 'vrSYXqLC+9k=' 'jsoncpp integer/escape/sorting algorithm'
Throws {ConvertTo-LocalCoopJsonCppText -Value 1.5} 'Floating-point' 'floating-point tag input refused'

# Compile declarations only: no Win32 call, window, process enumeration/input.
$module=Get-Module LocalCoopWindows
& $module {Initialize-LocalCoopWindows}
Check ([bool]('MgsCoopLocal.Windows' -as [type])) 'Win32 interop compiles under current PowerShell'
$text=[IO.File]::ReadAllText($modulePath)
Check ($text -notmatch 'SetForegroundWindow|SendInput|SendMessage|PostMessage|SwitchDesktop|ShowWindow') 'module has no focus/input/desktop APIs'
Check ($text.Contains('$target.Height,0x14)')) 'placement explicitly NOZORDER and NOACTIVATE'
Check (@([IO.File]::ReadAllBytes($modulePath) | Where-Object {$_ -gt 127}).Count -eq 0) 'friend module ASCII'
Write-Output ("PASS: {0} local co-op window checks; no MGSV launch and no window." -f $script:checks)
