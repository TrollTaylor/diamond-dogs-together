#Requires -Version 7
<#
.SYNOPSIS
Captures a named save seed from the copied saves of a finished hidden-worker run.
.DESCRIPTION
The save-seed library (docs/test-boot.md "Save-seed library"): a later run starts
from the seed with Invoke-WorkerScenario / Start-WorkerSession -SaveSeed <name>
(or -HostSaveSeed / -GuestSaveSeed <name>).

The source is only a closed worker run's own <role>-saves folder (its session.json
names the run and says status closed); never the Steam userdata saves, which
Assert-WorkerPlainPath refuses outright. Only the files directly in 287700\local
and remote are taken. The seed is written next to, never inside, a checkout:
<SeedRoot>\<name>\{host,guest}\{287700\local,remote} plus manifest.json (each
file's size and SHA-256, a set hash per role, the source run). SeedRoot is
-SeedRoot, else $env:MGS_COOP_SEED_ROOT, else D:\mgs-coop-seeds. An existing seed
is replaced only with -Force, and then moved aside to <SeedRoot>\.replaced\.
.EXAMPLE
pwsh -File tools/worker/Save-WorkerSeed.ps1 -RunId 0123456789abcdef0123456789abcdef -Name africa-jeep
.EXAMPLE
pwsh -File tools/worker/Save-WorkerSeed.ps1 -RunDirectory $env:USERPROFILE\mgs-coop-run-archive\test-boot\<id> -Name mb-ladder -Note 'MB command deck, real seed'
#>
[CmdletBinding()]
param(
    # A closed worker run folder (anywhere, e.g. the run archive), or -RunId under <repo>\build\worker-runtime.
    [string]$RunDirectory,
    [ValidatePattern('^$|^[0-9a-f]{32}$')][string]$RunId='',
    [Parameter(Mandatory)][string]$Name,
    [ValidateSet('host','guest')][string[]]$Roles=@('host','guest'),
    [string]$SeedRoot='',
    [ValidateLength(0,200)][string]$Note='',
    [switch]$Force
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
Import-Module (Join-Path $PSScriptRoot 'WorkerSessionCommon.psm1') -Force -DisableNameChecking
if(!(Test-WorkerSeedName $Name)){throw "Invalid seed name '$Name': 2..32 of [a-z0-9-], starting with a letter (not real, lab, run, both or real-remote)."}
if(!!$RunDirectory -eq !!$RunId){throw 'Name exactly one of -RunDirectory and -RunId.'}
$run=if($RunId){Join-Path $repo ('build\worker-runtime\'+$RunId)}else{[IO.Path]::GetFullPath($RunDirectory)}
$run=Assert-WorkerPlainPath $run -MustExist
$id=[IO.Path]::GetFileName($run)
if(!(Test-WorkerRunId $id)){throw 'The run folder name is not a worker run id.'}
$session=Read-WorkerSessionJson (Join-Path $run 'session.json')
if([string]$session['run_id'] -cne $id){throw 'session.json names another run.'}
if([string]$session['status'] -cne 'closed'){throw "The run is not closed (session.json status '$($session['status'])'); seeds come only from finished runs."}
$root=Get-WorkerSeedRoot $SeedRoot
if((Test-WorkerDescendant $root $repo) -or (Test-WorkerDescendant $repo $root) -or $root -ieq $repo){throw 'The seed root must be outside the checkout.'}
$real=if($session['real_saves'] -is [Collections.IDictionary]){[string]$session['real_saves']['root']}else{''}
$rows=[ordered]@{}
foreach($role in $Roles){
    $source=Assert-WorkerPlainPath (Join-Path $run ($role+'-saves')) -MustExist
    if(!(Test-WorkerDescendant $source $run)){throw 'The copied saves are not inside the run.'}
    if($real -and ((Test-WorkerDescendant $source $real) -or $source -ieq [IO.Path]::GetFullPath($real))){throw 'Seeds never come from the real saves.'}
    $all=@(Get-WorkerCopyInventory $source)
    $take=@($all|Where-Object{[string]$_['path'] -cmatch '\A(287700\\local|remote)\\[^\\]+\z'})
    $skipped=@($all|Where-Object{[string]$_['path'] -cnotmatch '\A(287700\\local|remote)\\[^\\]+\z'}|ForEach-Object{[string]$_['path']})
    Assert-WorkerSeedRows $take "The $role copy of run $id"
    $rows[$role]=@{Source=$source;Files=$take;Skipped=$skipped}
}
if(!(Test-Path -LiteralPath $root)){$null=New-Item -ItemType Directory -Path $root}
$final=Assert-WorkerPlainPath (Join-Path $root $Name)
if((Test-Path -LiteralPath $final) -and !$Force){throw "Seed '$Name' exists in $root; pass -Force to replace it (the old one is moved to .replaced)."}
$stage=Assert-WorkerPlainPath (Join-Path $root ('.staging-'+$Name+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)))
$null=New-Item -ItemType Directory -Path $stage
try {
    $roleEntries=[ordered]@{}
    foreach($role in $rows.Keys){
        $r=$rows[$role]
        foreach($folder in @('287700\local','remote')){$null=New-Item -ItemType Directory -Path (Join-Path $stage "$role\$folder") -Force}
        foreach($f in $r.Files){
            $to=Join-Path $stage ($role+'\'+[string]$f['path'])
            [IO.File]::Copy((Join-Path $r.Source ([string]$f['path'])),$to,$false)
            Assert-WorkerHash $to ([string]$f['sha256'])
        }
        $copied=@(Get-WorkerCopyInventory (Join-Path $stage $role))
        $set=Get-WorkerSeedSetSha256 $copied
        if($set -cne (Get-WorkerSeedSetSha256 $r.Files)){throw "The $role seed copy differs from the run's copied saves."}
        $roleEntries[$role]=[ordered]@{count=$copied.Count;set_sha256=$set;skipped=@($r.Skipped);files=@($copied|ForEach-Object{[ordered]@{path=$_['path'];size=$_['size'];sha256=$_['sha256']}})}
    }
    $manifest=[ordered]@{format='mgs-coop-worker-seed';v='1';name=$Name;created_utc=[DateTime]::UtcNow.ToString('o');note=$Note
        source=[ordered]@{run_id=$id;run_dir=$run;session_status=[string]$session['status'];mode=$session['mode'];started_utc=$session['started_utc']
            save_seeds=$session['save_seeds'];lab_settings=@($session['lab_settings'])}
        roles=$roleEntries}
    Write-WorkerSessionJson (Join-Path $stage 'manifest.json') $manifest
    if(Test-Path -LiteralPath $final){
        $aside=Join-Path $root '.replaced'
        if(!(Test-Path -LiteralPath $aside)){$null=New-Item -ItemType Directory -Path $aside}
        $old=Join-Path $aside ($Name+'-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
        [IO.Directory]::Move($final,$old)
        Write-Host "Moved the previous '$Name' seed to $old"
    }
    [IO.Directory]::Move($stage,$final)
} catch {
    if(Test-Path -LiteralPath $stage){Remove-Item -LiteralPath $stage -Recurse -Force}
    throw
}
foreach($role in $rows.Keys){$null=Read-WorkerNamedSeed $root $Name $role}
Write-Host "Saved seed '$Name' ($(@($rows.Keys) -join ', ')) from run $id to $final"
[pscustomobject]@{Name=$Name;Path=$final;Roles=@($rows.Keys);RunId=$id;Manifest=(Join-Path $final 'manifest.json')}
