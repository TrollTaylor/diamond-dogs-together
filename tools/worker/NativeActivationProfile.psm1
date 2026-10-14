Set-StrictMode -Version Latest

function Assert-WorkerNativeActivationScope {
    param([bool]$Enabled,[string]$BuildProfile,[string]$Mode,[string]$PartnerProfile,
        [string]$NativeStage,[string]$SaveSeed,[bool]$AmmoExperiment)
    if (!$Enabled) { return }
    if ($BuildProfile -cnotin @('research','mgo-sleep-rest','mgo-unique-rest','mgo-standing-cycle') -or
        $Mode -cne 'lab' -or $PartnerProfile -cne 'native' -or $NativeStage -cne 'gun' -or
        $SaveSeed -cne 'lab' -or $AmmoExperiment) {
        throw 'Native activation observer requires explicit native/gun Research lab with copied lab saves and no ammo experiment.'
    }
}

function Add-WorkerNativeActivationExperiment {
    param([string[]]$Experiments,[bool]$Enabled)
    $flag='MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER'
    if (@($Experiments | Where-Object { $_ -ceq $flag }).Count) {
        throw 'Activation experiment must be added exactly once by the explicit observer option.'
    }
    if ($Enabled) {
        foreach ($required in @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST')) {
            if ($Experiments -cnotcontains $required) { throw 'Activation observer requires the complete stock46 native gun base.' }
        }
        return @($Experiments)+$flag
    }
    return @($Experiments)
}

Export-ModuleMember -Function Assert-WorkerNativeActivationScope, Add-WorkerNativeActivationExperiment
