#Requires -Version 7
# Pure catalog/restore fixtures. Never prepare or launch a worker.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\worker\WorkerSessionCommon.psm1') -Force -DisableNameChecking
$script:checks=0
function Check([bool]$Ok,[string]$Why){$script:checks++;if(!$Ok){throw $Why}}
function Refused([scriptblock]$Action,[string]$Why){$hit=$false;try{&$Action}catch{$hit=$true};Check $hit $Why}
$pins=Get-WorkerRuntimePins;$unique=Get-WorkerMgoUniqueRestPins
Check ($unique.Bytes -eq 16360448 -and $unique.Motions -eq 21 -and $unique.Capacity -eq 0) 'Exact unique catalog'
Check ($pins.Archive -ceq '87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96') 'Normal catalog unchanged'
Assert-WorkerMgoUniqueRestScope 'mgo-unique-rest' 'lab' 'native' 'gun' $false 'D:\candidate.dat' '' ''
Refused {Assert-WorkerMgoUniqueRestScope 'normal' 'lab' 'native' 'gun' $false 'D:\candidate.dat' '' ''} 'Normal argument rejected'
Refused {Assert-WorkerMgoUniqueRestScope 'mgo-unique-rest' 'play' 'native' 'gun' $false 'D:\candidate.dat' '' ''} 'Play rejected'
Refused {Assert-WorkerMgoUniqueRestScope 'mgo-unique-rest' 'lab' 'native' 'gun' $false 'D:\candidate.dat' 'D:\buddy.dat' ''} 'Buddy mix rejected'
Refused {Assert-WorkerMgoUniqueRestScope 'mgo-unique-rest' 'lab' 'native' 'gun' $false 'D:\candidate.dat' '' 'D:\old.dat'} 'Old resident mix rejected'
Refused {Assert-WorkerMgoUniqueRestScope 'mgo-unique-rest' 'lab' 'native' 'gun' $true 'D:\candidate.dat' '' ''} 'Ammo rejected'
$session=@{mode='lab';candidate_sha256=('A'*64);runtime_profile=$unique.Profile;runtime_archive_sha256=$unique.Archive;
 runtime_archive_bytes=$unique.Bytes;runtime_motion_count=21;runtime_buddy_capacity=0;runtime_catalog_sha256=$unique.Catalog;
 runtime_build_profile='mgo-unique-rest';runtime_native_stage='gun'}
$runtime=Get-WorkerSessionRuntimeCatalog $session $pins
Check ($runtime.Experimental -and $runtime.Profile -ceq 'mgo_unique_rest_v1') 'Exact session accepted'
Check ((Get-WorkerRestorePair $session $pins ('B'*64) ('B'*64) $pins.Archive) -ceq 'baseline') 'Original installation restore pair'
Check ((Get-WorkerRestorePair $session $pins ('B'*64) ('A'*64) $unique.Archive) -ceq 'worker') 'Owned unique installation restore pair'
Refused {Get-WorkerRestorePair $session $pins ('B'*64) ('A'*64) $pins.Archive} 'Mixed worker DLL/baseline archive rejected'
Refused {Get-WorkerRestorePair $session $pins ('B'*64) ('B'*64) $unique.Archive} 'Mixed baseline DLL/unique archive rejected'
foreach($key in @('runtime_catalog_sha256','runtime_native_stage','runtime_archive_bytes')){
 $missing=$session.Clone();$missing.Remove($key);Refused {Get-WorkerSessionRuntimeCatalog $missing $pins} ('Partial session '+$key)
}
$bad=$session.Clone();$bad.runtime_build_profile='research';Refused {Get-WorkerSessionRuntimeCatalog $bad $pins} 'Generic research restore rejected'
$bad=$session.Clone();$bad.runtime_archive_sha256=(Get-WorkerMgoSleepRestPins).Archive;Refused {Get-WorkerSessionRuntimeCatalog $bad $pins} 'Old candidate hash rejected'
$bad=$session.Clone();$bad.runtime_motion_count=2;Refused {Get-WorkerSessionRuntimeCatalog $bad $pins} 'Native bank count not direct-motion count'
$bad=$session.Clone();$bad.mode='play';Refused {Get-WorkerSessionRuntimeCatalog $bad $pins} 'Play restore metadata rejected'
$experiments=@(Get-WorkerMgoUniqueRestExperiments)
Check ($experiments.Count -eq 4 -and $experiments -ccontains 'MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER' -and $experiments -cnotcontains 'MGS_LOCAL_MGO_UNIQUE_REST_BIND') 'Append absent from exact profile'
'mgo_unique_rest_session_tests: '+$script:checks+' pure checks passed; no worker operations'
