# Local two-window layout helpers. Windows PowerShell 5.1; no game input.
# Importing/calling pure helpers does not enumerate or move windows.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-LocalCoopProperty($Object, [string]$Name, $Default = $null) {
    if ($null -eq $Object) { return $Default }
    if ($Object -is [Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
    } elseif ($Object.PSObject.Properties[$Name]) { return $Object.PSObject.Properties[$Name].Value }
    return $Default
}
function Get-LocalCoopRectangleKey($Rectangle) {
    return '{0},{1},{2},{3}' -f $Rectangle.X, $Rectangle.Y, $Rectangle.Width, $Rectangle.Height
}
function Get-LocalCoopLayout {
    # Pure physical-pixel OUTER rectangles within this monitor's working area.
    # MGSV's verified client minimum is640x480. Use vertical tiling only if
    # side-by-side cannot fit; refuse rather than knowingly overlap windows.
    [CmdletBinding()]
    param([Parameter(Mandatory)]$WorkingArea,[ValidateRange(48,768)][int]$Dpi=96,
          [ValidateRange(0,128)][int]$Gap=8)
    $x=[long](Get-LocalCoopProperty $WorkingArea 'X' (Get-LocalCoopProperty $WorkingArea 'Left' 0))
    $y=[long](Get-LocalCoopProperty $WorkingArea 'Y' (Get-LocalCoopProperty $WorkingArea 'Top' 0))
    $width=[long](Get-LocalCoopProperty $WorkingArea 'Width' 0)
    $height=[long](Get-LocalCoopProperty $WorkingArea 'Height' 0)
    if($width -lt 2 -or $height -lt 1 -or $width -gt 32768 -or $height -gt 32768 -or
       $x -lt [int]::MinValue -or $y -lt [int]::MinValue -or
       $x+$width -gt [int]::MaxValue -or $y+$height -gt [int]::MaxValue) {
        throw 'The monitor working area has an invalid rectangle.'
    }
    if($Dpi -eq 96 -and (Get-LocalCoopProperty $WorkingArea 'Dpi' 0)) {
        $observedDpi=[int](Get-LocalCoopProperty $WorkingArea 'Dpi' 96)
        if($observedDpi -lt 48 -or $observedDpi -gt 768) {throw 'The monitor DPI is invalid.'}
        $Dpi=$observedDpi
    }
    $scaledGap=[int][Math]::Round($Gap*$Dpi/96.0)
    # Conservative themed frame/caption estimates determine render resolution.
    # All final placements use physical outer dimensions and DPI awareness.
    $frame=[int][Math]::Ceiling(8*$Dpi/96.0)
    $caption=[int][Math]::Ceiling(23*$Dpi/96.0)
    $extraWidth=2*$frame;$extraHeight=2*$frame+$caption
    $minimumWidth=640+$extraWidth;$minimumHeight=480+$extraHeight
    if($width -ge 2*$minimumWidth+$scaledGap -and $height -ge $minimumHeight) {
        $orientation='side-by-side'
        $hostWidth=[int][Math]::Floor(($width-$scaledGap)/2.0)
        $guestWidth=[int]($width-$scaledGap-$hostWidth)
        $clientWidth=[int][Math]::Min(7680,$hostWidth-$extraWidth)
        $clientHeight=[int][Math]::Min([Math]::Min(4320,$height-$extraHeight),[Math]::Max(480,[Math]::Ceiling($clientWidth*9.0/16.0)))
        $outerHeight=$clientHeight+$extraHeight
        $top=[int]($y+[Math]::Floor(($height-$outerHeight)/2.0))
        $host=[pscustomobject]@{X=[int]$x;Y=$top;Width=$hostWidth;Height=$outerHeight;ClientWidth=$clientWidth;ClientHeight=$clientHeight}
        $guest=[pscustomobject]@{X=[int]($x+$hostWidth+$scaledGap);Y=$top;Width=$guestWidth;Height=$outerHeight
            ClientWidth=[int][Math]::Min(7680,$guestWidth-$extraWidth);ClientHeight=$clientHeight}
    } elseif($width -ge $minimumWidth -and $height -ge 2*$minimumHeight+$scaledGap) {
        $orientation='vertical'
        $slotHeight=[int][Math]::Floor(($height-$scaledGap)/2.0)
        $clientHeight=[int][Math]::Min(4320,$slotHeight-$extraHeight)
        $clientWidth=[int][Math]::Min([Math]::Min(7680,$width-$extraWidth),[Math]::Max(640,[Math]::Floor($clientHeight*16.0/9.0)))
        $outerWidth=$clientWidth+$extraWidth;$outerHeight=$clientHeight+$extraHeight
        $left=[int]($x+[Math]::Floor(($width-$outerWidth)/2.0))
        $host=[pscustomobject]@{X=$left;Y=[int]$y;Width=$outerWidth;Height=$outerHeight;ClientWidth=$clientWidth;ClientHeight=$clientHeight}
        $guest=[pscustomobject]@{X=$left;Y=[int]($y+$outerHeight+$scaledGap);Width=$outerWidth;Height=$outerHeight
            ClientWidth=$clientWidth;ClientHeight=$clientHeight}
    } else {
        throw 'This display is too small for two MGSV windows at640x480 with their window borders. Use a larger display or reduce Windows display scaling.'
    }
    return [pscustomobject]@{Host=$host;Guest=$guest;Dpi=$Dpi;Gap=$scaledGap;Orientation=$orientation
        WorkingArea=[pscustomobject]@{X=[int]$x;Y=[int]$y;Width=[int]$width;Height=[int]$height}}
}
function Initialize-LocalCoopProcessImage {
    # Image-path and file-identity reads only. No process creation, window, or input.
    if ('MgsCoopLocal.ProcessImage' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace MgsCoopLocal {
public static class ProcessImage {
    [StructLayout(LayoutKind.Sequential)]
    struct Info {
        public uint FileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern bool QueryFullProcessImageName(IntPtr process, int flags, StringBuilder name, ref int size);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern int GetLongPathName(string shortPath, StringBuilder longPath, int size);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern int GetShortPathName(string longPath, StringBuilder shortPath, int size);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetFileInformationByHandle(SafeFileHandle file, out Info info);
    public static string Query(IntPtr process) {
        if (process == IntPtr.Zero) return "";
        StringBuilder name = new StringBuilder(32768);
        int size = name.Capacity;
        if (!QueryFullProcessImageName(process, 0, name, ref size) || size <= 0) return "";
        return name.ToString();
    }
    public static string ToLongPath(string path) {
        if (string.IsNullOrEmpty(path)) return "";
        StringBuilder name = new StringBuilder(32768);
        int n = GetLongPathName(path, name, name.Capacity);
        if (n <= 0 || n >= name.Capacity) return "";
        return name.ToString();
    }
    public static string ToShortPath(string path) {
        if (string.IsNullOrEmpty(path)) return "";
        StringBuilder name = new StringBuilder(32768);
        int n = GetShortPathName(path, name, name.Capacity);
        if (n <= 0 || n >= name.Capacity) return "";
        return name.ToString();
    }
    public static string FileId(string path) {
        SafeFileHandle handle = CreateFileW(path, 0x80000000u, 7u, IntPtr.Zero, 3u, 0x02000000u, IntPtr.Zero);
        if (handle.IsInvalid) return "";
        try {
            Info info;
            if (!GetFileInformationByHandle(handle, out info)) return "";
            return info.VolumeSerialNumber.ToString("X8") + ":" + info.FileIndexHigh.ToString("X8") + ":" + info.FileIndexLow.ToString("X8") + ":" + info.NumberOfLinks.ToString();
        } finally { handle.Dispose(); }
    }
}
}
'@
}
function ConvertTo-LocalCoopComparablePath([AllowNull()][AllowEmptyString()][string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    $p = $Path.Trim().Replace('/', '\')
    if ($p.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) { $p = '\\' + $p.Substring(8) }
    elseif ($p.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { $p = $p.Substring(4) }
    try { return [IO.Path]::GetFullPath($p).TrimEnd('\') } catch { return '' }
}
function Test-LocalCoopImageBelongsToGame {
    # True only for this copy's mgsvtpp.exe. A hardlink in another directory
    # is the same file and still does not belong to this copy.
    [CmdletBinding()]
    param([AllowNull()][AllowEmptyString()][string]$ImagePath, [AllowNull()][AllowEmptyString()][string]$GameRoot)
    try {
        if ([string]::IsNullOrWhiteSpace($ImagePath) -or [string]::IsNullOrWhiteSpace($GameRoot)) { return $false }
        $root = ConvertTo-LocalCoopComparablePath $GameRoot
        if (!$root -or ![IO.Path]::IsPathRooted($root)) { return $false }
        $expected = ConvertTo-LocalCoopComparablePath (Join-Path $root 'mgsvtpp.exe')
        if (!$expected) { return $false }
        $actual = ConvertTo-LocalCoopComparablePath $ImagePath
        if ($actual -and $expected.Equals($actual, [StringComparison]::OrdinalIgnoreCase)) { return $true }
        Initialize-LocalCoopProcessImage
        $long = ConvertTo-LocalCoopComparablePath ([MgsCoopLocal.ProcessImage]::ToLongPath($ImagePath))
        return [bool]($long -and $expected.Equals($long, [StringComparison]::OrdinalIgnoreCase))
    } catch { return $false }
}
function Select-LocalCoopOwnedImagePath {
    # First candidate that is this copy's executable. Empty and other copies lose.
    [CmdletBinding()]
    param([string[]]$Candidates = @(), [Parameter(Mandatory)][string]$GameRoot)
    foreach ($candidate in @($Candidates)) {
        if (Test-LocalCoopImageBelongsToGame -ImagePath $candidate -GameRoot $GameRoot) {
            return (ConvertTo-LocalCoopComparablePath $candidate)
        }
    }
    return $null
}
function Get-LocalCoopOwnedImagePath {
    # Process.Path (MainModule.FileName) is often empty immediately after
    # UseShellExecute on Windows PowerShell 5.1. QueryFullProcessImageName
    # already names the image of the process the caller started.
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Process, [Parameter(Mandatory)][string]$GameRoot, [ValidateRange(1,40)][int]$Attempts = 1)
    Initialize-LocalCoopProcessImage
    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        try { if ($Process.HasExited) { return $null } } catch { return $null }
        $candidates = @()
        try {
            $handle = [IntPtr]$Process.Handle
            $queried = [MgsCoopLocal.ProcessImage]::Query($handle)
            if ($queried) { $candidates += $queried }
        } catch {}
        try {
            $main = $Process.MainModule
            if ($main -and $main.FileName) { $candidates += [string]$main.FileName }
        } catch {}
        try {
            $path = [string]$Process.Path
            if ($path) { $candidates += $path }
        } catch {}
        $selected = Select-LocalCoopOwnedImagePath -Candidates $candidates -GameRoot $GameRoot
        if ($selected) { return $selected }
        try {
            $cim = Get-CimInstance -ClassName Win32_Process -Filter ("ProcessId=" + [int]$Process.Id) -ErrorAction Stop
            if ($cim -and $cim.ExecutablePath) {
                $selected = Select-LocalCoopOwnedImagePath -Candidates @([string]$cim.ExecutablePath) -GameRoot $GameRoot
                if ($selected) { return $selected }
            }
        } catch {}
        if (($attempt + 1) -lt $Attempts) { Start-Sleep -Milliseconds 50 }
    }
    return $null
}
function Test-LocalCoopOwnerSnapshot {
    # Pure validation also used immediately before each window mutation.
    # PID and creation time must be the process the launcher started, and both
    # image paths must be this copy's mgsvtpp.exe (long path or Win32 \\?\ form).
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Owner,[Parameter(Mandatory)][string]$GameRoot,
          [Parameter(Mandatory)]$Snapshot)
    try {
        $ownerId=[int](Get-LocalCoopProperty $Owner 'ProcessId' 0)
        if($ownerId -le 0 -or $ownerId -ne [int](Get-LocalCoopProperty $Snapshot 'ProcessId' 0)) {return $false}
        $ownedPath=[string](Get-LocalCoopProperty $Owner 'ExecutablePath' '')
        $actualPath=[string](Get-LocalCoopProperty $Snapshot 'ExecutablePath' '')
        if(!(Test-LocalCoopImageBelongsToGame -ImagePath $ownedPath -GameRoot $GameRoot)) {return $false}
        if(!(Test-LocalCoopImageBelongsToGame -ImagePath $actualPath -GameRoot $GameRoot)) {return $false}
        $ownedTime=[DateTime]::Parse([string](Get-LocalCoopProperty $Owner 'StartUtc' ''),[Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        $actualTime=[DateTime]::Parse([string](Get-LocalCoopProperty $Snapshot 'StartUtc' ''),[Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        return $ownedTime.Ticks -eq $actualTime.Ticks
    } catch {return $false}
}
function Get-LocalCoopOwnedProcess($Owner,[string]$GameRoot) {
    try {
        $process=Get-Process -Id ([int](Get-LocalCoopProperty $Owner 'ProcessId' 0)) -ErrorAction Stop
        $image=Get-LocalCoopOwnedImagePath -Process $process -GameRoot $GameRoot -Attempts 3
        $snapshot=[pscustomobject]@{ProcessId=$process.Id;StartUtc=$process.StartTime.ToUniversalTime().ToString('o');ExecutablePath=$(if($image){$image}else{''})}
        if(!(Test-LocalCoopOwnerSnapshot -Owner $Owner -GameRoot $GameRoot -Snapshot $snapshot)) {return $null}
        return $process
    } catch {return $null}
}
function New-LocalCoopWindowLayoutState {return [pscustomobject]@{Roles=@{}}}
function Get-LocalCoopPlacementDecision {
    # Pure bounded placement policy. Position-only dragging is respected.
    # At most2 size-reset corrections in6seconds; Retile is user-requested.
    [CmdletBinding()]
    param($Entry,[Parameter(Mandatory)][long]$Handle,[Parameter(Mandatory)]$Rectangle,
          [Parameter(Mandatory)]$Target,[Parameter(Mandatory)][DateTime]$NowUtc,
          [switch]$Retile,[switch]$Moving,[switch]$Overlapping)
    $key=Get-LocalCoopRectangleKey $Rectangle
    $newHandle=!$Entry -or [long](Get-LocalCoopProperty $Entry 'Handle' 0) -ne $Handle
    if($newHandle) {
        $Entry=[pscustomobject]@{Handle=$Handle;SeenUtc=$NowUtc;StableSinceUtc=$NowUtc
            LastObservedKey=$key;PlacedUtc=$null;DeadlineUtc=$null;Corrections=0;UserArranged=$false}
    } else {
        $Entry=[pscustomobject]@{Handle=$Entry.Handle;SeenUtc=$Entry.SeenUtc;StableSinceUtc=$Entry.StableSinceUtc
            LastObservedKey=$Entry.LastObservedKey;PlacedUtc=$Entry.PlacedUtc;DeadlineUtc=$Entry.DeadlineUtc;Corrections=$Entry.Corrections;UserArranged=[bool](Get-LocalCoopProperty $Entry 'UserArranged' $false)}
        if($Entry.LastObservedKey -cne $key) {$Entry.LastObservedKey=$key;$Entry.StableSinceUtc=$NowUtc}
    }
    $place=$false;$reason='settled'
    if($Moving) {$reason='user_moving';$Entry.UserArranged=$true}
    elseif($Retile) {$place=$true;$reason='retile';$Entry.PlacedUtc=$NowUtc;$Entry.DeadlineUtc=$NowUtc.AddSeconds(6);$Entry.Corrections=0;$Entry.UserArranged=$false}
    elseif($Entry.UserArranged) {$reason='user_arranged'}
    elseif(!$Entry.PlacedUtc) {
        if(($NowUtc-$Entry.SeenUtc).TotalMilliseconds -ge 300 -and ($NowUtc-$Entry.StableSinceUtc).TotalMilliseconds -ge 300) {
            $place=$true;$reason='initial';$Entry.PlacedUtc=$NowUtc;$Entry.DeadlineUtc=$NowUtc.AddSeconds(6)
        } else {$reason='window_settling'}
    } elseif($NowUtc -lt $Entry.DeadlineUtc -and $Entry.Corrections -lt 2 -and
        ([Math]::Abs($Rectangle.Width-$Target.Width) -gt 2 -or [Math]::Abs($Rectangle.Height-$Target.Height) -gt 2 -or
         ($Overlapping -and ([Math]::Abs($Rectangle.X-$Target.X) -gt 2 -or [Math]::Abs($Rectangle.Y-$Target.Y) -gt 2)))) {
        if(($NowUtc-$Entry.PlacedUtc).TotalMilliseconds -ge 750 -and ($NowUtc-$Entry.StableSinceUtc).TotalMilliseconds -ge 500) {
            $place=$true;$reason='startup_reset';$Entry.PlacedUtc=$NowUtc;$Entry.Corrections++
        } else {$reason='reset_debounce'}
    }
    return [pscustomobject]@{Place=$place;Reason=$reason;Entry=$Entry}
}
function Initialize-LocalCoopWindows {
    if('MgsCoopLocal.Windows' -as [type]) {return}
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
namespace MgsCoopLocal {
public static class Windows {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO {public uint Size;public RECT Monitor,Work;public uint Flags;}
    [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {
        public uint Size,Flags;public IntPtr Active,Focus,Capture,MenuOwner,MoveSize,Caret;public RECT CaretRect;
    }
    public sealed class WindowInfo {public IntPtr Handle;public int X,Y,Width,Height;public bool Moving;}
    private delegate bool EnumCallback(IntPtr hwnd,IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback,IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr hwnd,uint command);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd,out RECT rect);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr hwnd,out RECT rect);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint process);
    [DllImport("user32.dll")] private static extern bool GetGUIThreadInfo(uint thread,ref GUITHREADINFO info);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr hwnd,uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern bool GetMonitorInfo(IntPtr monitor,ref MONITORINFO info);
    [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("shcore.dll")] private static extern int GetDpiForMonitor(IntPtr monitor,int type,out uint x,out uint y);
    [DllImport("user32.dll",SetLastError=true)] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int width,int height,uint flags);
    public static IntPtr BeginDpi() {
        try {IntPtr old=SetThreadDpiAwarenessContext(new IntPtr(-4));if(old==IntPtr.Zero) old=SetThreadDpiAwarenessContext(new IntPtr(-3));return old;}
        catch(EntryPointNotFoundException) {return IntPtr.Zero;}
    }
    public static void EndDpi(IntPtr old) {if(old!=IntPtr.Zero) {try {SetThreadDpiAwarenessContext(old);} catch(EntryPointNotFoundException) {}}}
    public static int[] WorkArea(IntPtr reference) {
        if(reference==IntPtr.Zero) reference=GetConsoleWindow();
        if(reference==IntPtr.Zero || !IsWindowVisible(reference)) reference=GetForegroundWindow();
        IntPtr monitor=MonitorFromWindow(reference,2);
        MONITORINFO info=new MONITORINFO();info.Size=(uint)Marshal.SizeOf(typeof(MONITORINFO));
        if(monitor==IntPtr.Zero || !GetMonitorInfo(monitor,ref info)) throw new InvalidOperationException("Cannot read monitor working area.");
        uint dx=96,dy=96;
        try {uint a,b;if(GetDpiForMonitor(monitor,0,out a,out b)==0) {dx=a;dy=b;}}
        catch(DllNotFoundException) {} catch(EntryPointNotFoundException) {}
        try {uint actual=GetDpiForWindow(reference);if(actual!=0) dx=actual;} catch(EntryPointNotFoundException) {}
        return new int[]{info.Work.Left,info.Work.Top,info.Work.Right-info.Work.Left,info.Work.Bottom-info.Work.Top,(int)dx};
    }
    public static WindowInfo[] ForProcess(int process) {
        List<WindowInfo> found=new List<WindowInfo>();
        EnumWindows(delegate(IntPtr hwnd,IntPtr data) {
            uint pid;uint tid=GetWindowThreadProcessId(hwnd,out pid);
            if(pid!=(uint)process || !IsWindowVisible(hwnd) || IsIconic(hwnd) || GetWindow(hwnd,4)!=IntPtr.Zero) return true;
            RECT r,c;
            if(!GetWindowRect(hwnd,out r) || !GetClientRect(hwnd,out c) || c.Right-c.Left<200 || c.Bottom-c.Top<100) return true;
            GUITHREADINFO gui=new GUITHREADINFO();gui.Size=(uint)Marshal.SizeOf(typeof(GUITHREADINFO));
            bool moving=GetGUIThreadInfo(tid,ref gui) && ((gui.Flags&2)!=0);
            found.Add(new WindowInfo{Handle=hwnd,X=r.Left,Y=r.Top,Width=r.Right-r.Left,Height=r.Bottom-r.Top,Moving=moving});return true;
        },IntPtr.Zero);
        return found.ToArray();
    }
}
}
'@
}
function Get-LocalCoopWorkingArea {
    [CmdletBinding()]
    param([IntPtr]$ReferenceHandle=[IntPtr]::Zero)
    Initialize-LocalCoopWindows
    $old=[MgsCoopLocal.Windows]::BeginDpi()
    try {$a=[MgsCoopLocal.Windows]::WorkArea($ReferenceHandle);return [pscustomobject]@{X=$a[0];Y=$a[1];Width=$a[2];Height=$a[3];Dpi=$a[4]}}
    finally {[MgsCoopLocal.Windows]::EndDpi($old)}
}
function Set-LocalCoopWindowLayout {
    <#
    .SYNOPSIS
    Places only caller-owned game windows without activation.
    .PARAMETER Instances
    Dictionary host/guest => ProcessId,StartUtc (ISO UTC),ExecutablePath.
    .PARAMETER GameRoots
    Dictionary host/guest => exact game folder.
    .PARAMETER State
    Keep New-LocalCoopWindowLayoutState across250-500ms startup polls.
    Initial handles settle300ms. At most2 corrections in6seconds; later
    polls do not undo dragging. Recreated handles get new initial placement.
    Retile explicitly requests both windows be placed again.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][Collections.IDictionary]$Instances,
          [Parameter(Mandatory)][Collections.IDictionary]$GameRoots,$WorkingArea,$State,[switch]$Retile)
    foreach($role in 'host','guest') {
        if(!$Instances.Contains($role) -or !$GameRoots.Contains($role)) {throw "Missing owned $role process/root."}
    }
    if(!$State) {$State=New-LocalCoopWindowLayoutState}
    if(!$WorkingArea) {$WorkingArea=Get-LocalCoopWorkingArea}
    $layout=Get-LocalCoopLayout -WorkingArea $WorkingArea
    Initialize-LocalCoopWindows
    $old=[MgsCoopLocal.Windows]::BeginDpi();$results=@()
    try {
        foreach($role in 'host','guest') {
            $owner=$Instances[$role];$root=[string]$GameRoots[$role]
            if(!(Get-LocalCoopOwnedProcess $owner $root)) {
                $results+=@([pscustomobject]@{Role=$role;Placed=$false;Reason='owner_not_live';Handle=0});continue
            }
            $windows=@([MgsCoopLocal.Windows]::ForProcess([int]$owner.ProcessId) | Sort-Object @{Expression={$_.Width*$_.Height};Descending=$true})
            if(!$windows.Count) {$results+=@([pscustomobject]@{Role=$role;Placed=$false;Reason='render_window_pending';Handle=0});continue}
            $window=$windows[0];$target=if($role -eq 'host'){$layout.Host}else{$layout.Guest}
            $otherRole=if($role -eq 'host'){'guest'}else{'host'};$overlap=$false
            if(Get-LocalCoopOwnedProcess $Instances[$otherRole] ([string]$GameRoots[$otherRole])) {
                foreach($other in @([MgsCoopLocal.Windows]::ForProcess([int]$Instances[$otherRole].ProcessId))) {
                    if($window.X -lt $other.X+$other.Width -and $other.X -lt $window.X+$window.Width -and
                       $window.Y -lt $other.Y+$other.Height -and $other.Y -lt $window.Y+$window.Height) {$overlap=$true;break}
                }
            }
            $decision=Get-LocalCoopPlacementDecision -Entry $State.Roles[$role] -Handle $window.Handle.ToInt64() -Rectangle $window -Target $target -NowUtc ([DateTime]::UtcNow) -Retile:$Retile -Moving:$window.Moving -Overlapping:$overlap
            $State.Roles[$role]=$decision.Entry;$placed=$false;$reason=$decision.Reason;$actual=$null;$matched=$false
            if($decision.Place) {
                [uint32]$windowProcess=0;$null=[MgsCoopLocal.Windows]::GetWindowThreadProcessId($window.Handle,[ref]$windowProcess)
                if(!(Get-LocalCoopOwnedProcess $owner $root) -or $windowProcess -ne [uint32]$owner.ProcessId -or ![MgsCoopLocal.Windows]::IsWindow($window.Handle)) {$reason='owner_changed'}
                else {
                    # SWP_NOZORDER | SWP_NOACTIVATE. No focus/input APIs.
                    $placed=[MgsCoopLocal.Windows]::SetWindowPos($window.Handle,[IntPtr]::Zero,$target.X,$target.Y,$target.Width,$target.Height,0x14)
                    if(!$placed) {$reason='placement_refused'}
                }
                if($placed) {
                    $readback=@([MgsCoopLocal.Windows]::ForProcess([int]$owner.ProcessId) | Where-Object {$_.Handle -eq $window.Handle})
                    if($readback.Count -eq 1) {
                        $actual=$readback[0]
                        $matched=[Math]::Abs($actual.X-$target.X) -le 2 -and [Math]::Abs($actual.Y-$target.Y) -le 2 -and [Math]::Abs($actual.Width-$target.Width) -le 2 -and [Math]::Abs($actual.Height-$target.Height) -le 2
                    }
                    if(!$matched) {$reason='game_size_not_applied'}
                }
            }
            $results+=@([pscustomobject]@{Role=$role;Placed=$placed;Reason=$reason;Handle=$window.Handle.ToInt64();Target=$target;Actual=$actual;FitsRequestedRectangle=$matched})
        }
    } finally {[MgsCoopLocal.Windows]::EndDpi($old)}
    return [pscustomobject]@{Layout=$layout;State=$State;Results=$results}
}

# Graphics helpers adapted from this project tools\lab\LabCommon.psm1.
# Same verified jsoncpp tag algorithm; MD5.Create supports Windows PowerShell5.1.
function ConvertTo-LocalCoopJsonCppText {
    # jsoncpp FastWriter output for the value types TPP_GRAPHICS_CONFIG uses.
    param([AllowNull()]$Value, [switch]$Document)
    $text = if ($null -eq $Value) { 'null' }
    elseif ($Value -is [bool]) { if ($Value) { 'true' } else { 'false' } }
    elseif ($Value -is [string]) {
        $sb = [Text.StringBuilder]::new('"')
        foreach ($ch in $Value.ToCharArray()) {
            $code = [int]$ch
            if ($code -gt 0x7e) { throw 'Non-ASCII text is not supported in the graphics config.' }
            switch ($ch) {
                '"' { $null = $sb.Append('\"') }
                '\' { $null = $sb.Append('\\') }
                "`b" { $null = $sb.Append('\b') }
                "`f" { $null = $sb.Append('\f') }
                "`n" { $null = $sb.Append('\n') }
                "`r" { $null = $sb.Append('\r') }
                "`t" { $null = $sb.Append('\t') }
                default {
                    if ($code -lt 0x20) { $null = $sb.Append('\u' + $code.ToString('x4')) } else { $null = $sb.Append($ch) }
                }
            }
        }
        $sb.Append('"').ToString()
    }
    elseif ($Value -is [int] -or $Value -is [long] -or $Value -is [int16] -or $Value -is [byte] -or $Value -is [uint32] -or $Value -is [uint64]) {
        ([long]$Value).ToString([Globalization.CultureInfo]::InvariantCulture)
    }
    elseif ($Value -is [double] -or $Value -is [decimal] -or $Value -is [single]) { throw 'Floating-point values are not supported in the graphics config.' }
    elseif ($Value -is [Collections.IDictionary] -or $Value -is [Management.Automation.PSCustomObject]) {
        $names = if ($Value -is [Collections.IDictionary]) { @($Value.Keys | ForEach-Object { [string]$_ }) } else { @($Value.PSObject.Properties.Name) }
        foreach ($n in $names) { if ($n -match '[^\x20-\x7e]') { throw 'Non-ASCII keys are not supported.' } }
        [Array]::Sort($names, [StringComparer]::Ordinal)
        $parts = foreach ($n in $names) {
            $item = if ($Value -is [Collections.IDictionary]) { $Value[$n] } else { $Value.PSObject.Properties[$n].Value }
            (ConvertTo-LocalCoopJsonCppText -Value $n) + ':' + (ConvertTo-LocalCoopJsonCppText -Value $item)
        }
        '{' + (@($parts) -join ',') + '}'
    }
    elseif ($Value -is [Collections.IEnumerable]) {
        '[' + (@($Value | ForEach-Object { ConvertTo-LocalCoopJsonCppText -Value $_ }) -join ',') + ']'
    }
    else { throw "Unsupported JSON value type: $($Value.GetType().FullName)" }
    if ($Document) { return $text + "`n" }
    return $text
}

function Get-LocalCoopGraphicsTag {
    # The tag the game expects for a parsed config object (the "as" member is ignored).
    param([Parameter(Mandatory)]$Object)
    $copy = [ordered]@{}
    foreach ($p in $Object.PSObject.Properties) { if ($p.Name -cne 'as') { $copy[$p.Name] = $p.Value } }
    $serial = ConvertTo-LocalCoopJsonCppText -Value $copy -Document
    $bytes = [Text.Encoding]::ASCII.GetBytes($serial + '-' + $serial.Length.ToString('x'))
    $md5 = [Security.Cryptography.MD5]::Create(); try { $digest = $md5.ComputeHash($bytes) } finally { $md5.Dispose() }
    return [Convert]::ToBase64String($digest, 0, 8)
}

function Get-LocalCoopGraphicsInfo {
    # Pure: parses config text; Valid = the stored tag matches the computed one.
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Text)
    $info = [ordered]@{ Valid = $false; StoredTag = $null; ComputedTag = $null; WindowMode = $null; Width = 0; Height = 0; Error = $null }
    try {
        $object = $Text | ConvertFrom-Json
        $info.StoredTag = [string](Get-LocalCoopProperty $object 'as' '')
        $info.ComputedTag = Get-LocalCoopGraphicsTag -Object $object
        $video = $object.graphics.videoout_setting
        $info.WindowMode = [string]$video.window_mode
        $info.Width = [int]$video.width
        $info.Height = [int]$video.height
        $info.Valid = [bool]$info.StoredTag -and $info.StoredTag -ceq $info.ComputedTag
    } catch { $info.Error = $_.Exception.Message }
    return [pscustomobject]$info
}

function Set-LocalCoopGraphicsWindowedText {
    # Pure: returns the config text with the requested window mode and size and a
    # recomputed tag. Edits values in place (formatting is preserved) and refuses
    # unless the input tag verifies and every edited key occurs exactly once.
    param(
        [Parameter(Mandatory)][string]$Text,
        [int]$Width = 1280,
        [int]$Height = 720,
        [ValidateSet('Windowed', 'FlexibleWindowed', 'FullScreenWindowed', 'FullScreen')][string]$WindowMode = 'Windowed'
    )
    if ($Width -lt 640 -or $Width -gt 7680 -or $Height -lt 480 -or $Height -gt 4320) { throw 'Unsupported window size.' }
    $before = Get-LocalCoopGraphicsInfo -Text $Text
    if (!$before.Valid) { throw ('TPP_GRAPHICS_CONFIG tag does not verify; refusing to edit. ' + $before.Error) }
    $edits = @(
        @{ Pattern = '("window_mode"\s*:\s*")[A-Za-z]+(")'; Value = $WindowMode },
        @{ Pattern = '("width"\s*:\s*)[0-9]+()'; Value = [string]$Width },
        @{ Pattern = '("height"\s*:\s*)[0-9]+()'; Value = [string]$Height }
    )
    $result = $Text
    foreach ($edit in $edits) {
        $count = [regex]::Matches($result, $edit.Pattern).Count
        if ($count -ne 1) { throw "TPP_GRAPHICS_CONFIG has an unexpected layout ($count matches for $($edit.Pattern))." }
        $value = $edit.Value
        $result = [regex]::Replace($result, $edit.Pattern, { param($m) $m.Groups[1].Value + $value + $m.Groups[2].Value })
    }
    $object = $result | ConvertFrom-Json
    $tag = Get-LocalCoopGraphicsTag -Object $object
    $asPattern = '("as"\s*:\s*")[A-Za-z0-9+/=]+(")'
    if ([regex]::Matches($result, $asPattern).Count -ne 1) { throw 'TPP_GRAPHICS_CONFIG has an unexpected "as" layout.' }
    $result = [regex]::Replace($result, $asPattern, { param($m) $m.Groups[1].Value + $tag + $m.Groups[2].Value })
    $after = Get-LocalCoopGraphicsInfo -Text $result
    if (!$after.Valid -or $after.WindowMode -cne $WindowMode -or $after.Width -ne $Width -or $after.Height -ne $Height) {
        throw 'Internal error: edited TPP_GRAPHICS_CONFIG does not verify.'
    }
    return $result
}

# ---------------------------------------------------------------------------
Export-ModuleMember -Function Get-LocalCoopLayout,Get-LocalCoopWorkingArea,New-LocalCoopWindowLayoutState,Set-LocalCoopWindowLayout,Get-LocalCoopPlacementDecision,Initialize-LocalCoopProcessImage,ConvertTo-LocalCoopComparablePath,Test-LocalCoopImageBelongsToGame,Select-LocalCoopOwnedImagePath,Get-LocalCoopOwnedImagePath,Test-LocalCoopOwnerSnapshot,ConvertTo-LocalCoopJsonCppText,Get-LocalCoopGraphicsTag,Get-LocalCoopGraphicsInfo,Set-LocalCoopGraphicsWindowedText
