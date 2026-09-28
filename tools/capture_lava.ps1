param([string]$Binary='build/release/starfox_pc.exe',
    [string]$OutputDirectory='tmp/lava-review',
    [ValidateRange(0,1)][int]$RayTracing=1,
    [ValidateRange(0,3)][int]$Reflections=3,
    [ValidateRange(1,600)][int]$Frames=120,
    [ValidateRange(0,100000)][int]$PrerollTicks=120,
    [string]$Stage='LEVEL6_6', [int]$Ground=9, [int]$Sky=0,
    [ValidateSet('','Auto','Grass','Dirt','Sand','Snow','Water','Mirror','Gold Metal','Red Sand','Bubbling Lava')][string]$GroundMaterial='',
    [int]$ModelMaterial=0, [string]$Presses='', [int]$GroundEnabled=1,
    [ValidateRange(1,240)][int]$PressFrames=3,
    [int]$Fsr1=0, [ValidateRange(0,4)][int]$Dlss=0, [ValidateRange(0,7)][int]$Stereo=0,
    [ValidateRange(1,512)][int]$StereoSeparation=16,
    [ValidateRange(16,65535)][int]$StereoConvergence=1024,
    [ValidateRange(0,65535)][int]$StereoReticleDepth=0,
    [string]$DlssAdapter='', [string]$DlssBinaries='',
    [ValidateRange(0,3)][int]$Bloom=0, [string]$Display='16_9',
    [int]$ExMenu=-1, [switch]$D3d11, [switch]$Taa, [int]$AaType=0, [int]$AaQuality=0,
    [ValidateSet('GPU','SOFTWARE')][string]$Renderer='GPU', [ValidateRange(1,10)][int]$RenderScale=2,
    [ValidateSet('','vulkan','direct3d12','metal')][string]$GpuBackend='',
    [switch]$Profile, [switch]$Paced, [switch]$MenuPreview, [int]$ModelFx=0, [int]$WorldFx=0, [int]$WorldDistortion=0,
    [uint32]$GlobalEnhancements=0,
    [byte]$SceneEnhancements=0,
    [byte]$DepthEnhancements=0,
    [byte]$ParticleEnhancements=0,
    [ValidateRange(0,3)][byte]$Phosphor=0,
    [ValidateRange(0,3)][byte]$Exposure=0,
    [ValidateRange(0,3)][byte]$Caustics=0,
    [ValidateRange(0,3)][byte]$ShadowSoftness=2,
    [double]$CameraBank=0,
    [ValidateRange(0,63)][byte]$CameraResponse=0,
    [int]$CameraOffFrame=-1, [int]$CameraOnFrame=-1,
    [ValidateRange(-1,1)][int]$GodMode=-1,
    [switch]$TraceObjects,
    [switch]$VolumetricFog,
    [switch]$MotionBlur,
    [switch]$LiveMotionBlur,
    [switch]$CheckMotionPause,
    [switch]$CheckParticleShutter,
    [switch]$CheckGroundShadow,
    [switch]$CheckGroundReflection,
    [Alias('CheckTemporalLighting')][switch]$CheckTemporalSurfaces,
    [switch]$FailStereo,
    [switch]$FailStereoAfterLeft,
    [ValidateSet('EX','ORIGINAL')][string]$Experience='EX')
$ErrorActionPreference='Stop'
if($GroundMaterial) {
    $Ground=@{'Auto'=0;'Grass'=1;'Dirt'=2;'Sand'=3;'Snow'=4;'Water'=5;'Mirror'=6;'Gold Metal'=7;'Red Sand'=8;'Bubbling Lava'=9}[$GroundMaterial]
}
if($CheckMotionPause -and !$LiveMotionBlur) {throw '-CheckMotionPause requires -LiveMotionBlur and scripted pause/resume presses'}
if($CheckParticleShutter -and !$LiveMotionBlur) {throw '-CheckParticleShutter requires -LiveMotionBlur'}
if($CheckGroundShadow -and (!$LiveMotionBlur -or !$RayTracing)) {
    throw '-CheckGroundShadow requires -LiveMotionBlur with -RayTracing 1'
}
if($CheckGroundReflection -and (!$LiveMotionBlur -or !$RayTracing)) {
    throw '-CheckGroundReflection requires -LiveMotionBlur with -RayTracing 1'
}
if($CheckTemporalSurfaces -and (!$LiveMotionBlur -or !$Taa)) {throw '-CheckTemporalSurfaces requires -LiveMotionBlur and -Taa'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$saved=@{}
Get-ChildItem Env: | Where-Object {$_.Name -match '^(STARFOX_|SDL_AUDIODRIVER$|SDL_GPU_DRIVER$)'} | ForEach-Object {
    $saved[$_.Name]=$_.Value
    Remove-Item -LiteralPath "Env:$($_.Name)"
}
try {
    if($GpuBackend) { $env:SDL_GPU_DRIVER=$GpuBackend }
    elseif($saved.ContainsKey('SDL_GPU_DRIVER')) { $env:SDL_GPU_DRIVER=$saved['SDL_GPU_DRIVER'] }
    $settings=@{
        SDL_AUDIODRIVER='dummy';STARFOX_TEST_HIDDEN='1';STARFOX_TEST_FRAMES="$Frames";
        STARFOX_TEST_PREROLL_TICKS="$PrerollTicks";STARFOX_TEST_SKIP_PREROLL='1';STARFOX_TEST_EXPERIENCE=$Experience;
        STARFOX_TEST_RENDERER=$Renderer;STARFOX_TEST_DISPLAY_MODE=$Display;STARFOX_TEST_TIMING_MODE='ORIGINAL';
        STARFOX_TEST_PRESENTATION_FPS='60';STARFOX_TEST_UNPACED='1';STARFOX_TEST_VSYNC='0';
        STARFOX_TEST_RENDER_SCALE="$RenderScale";STARFOX_TEST_DLSS_SELECTION="$Dlss";STARFOX_TEST_STEREO_OUTPUT="$Stereo";
        STARFOX_TEST_STEREO_SEPARATION="$StereoSeparation";STARFOX_TEST_STEREO_CONVERGENCE="$StereoConvergence";
        STARFOX_TEST_ANTI_ALIASING="$AaQuality";STARFOX_TEST_AA_TYPE="$AaType";STARFOX_TEST_RTX_LIGHTING='0';STARFOX_TEST_2D_FILTER='0';
        STARFOX_TEST_BLOOM="$Bloom";STARFOX_TEST_BLOOM_2D='0';STARFOX_TEST_EFFECT='0';STARFOX_TEST_WORLD_EFFECT='0';
        STARFOX_TEST_MATERIAL="$ModelMaterial";STARFOX_TEST_MANIPULATION='0';STARFOX_TEST_MODEL_SMOOTHING='0';
        STARFOX_TEST_MODEL_FX="$ModelFx";STARFOX_TEST_WORLD_FX="$WorldFx";STARFOX_TEST_WORLD_DISTORTION="$WorldDistortion";
        STARFOX_TEST_GLOBAL_ENHANCEMENTS="$GlobalEnhancements";
        STARFOX_TEST_SCENE_ENHANCEMENTS="$SceneEnhancements";
        STARFOX_TEST_DEPTH_ENHANCEMENTS="$DepthEnhancements";
        STARFOX_TEST_PARTICLE_ENHANCEMENTS="$ParticleEnhancements";
        STARFOX_TEST_PHOSPHOR_PERSISTENCE="$Phosphor";
        STARFOX_TEST_ADAPTIVE_EXPOSURE="$Exposure";
        STARFOX_TEST_WATER_CAUSTICS="$Caustics";
        STARFOX_TEST_SHADOW_SOFTNESS="$ShadowSoftness";
        STARFOX_TRACE_SCENE_FX='1';
        STARFOX_TEST_HDR_EFFECT='0';STARFOX_TEST_CHROMATIC_ABERRATION='0';
        STARFOX_TEST_RAY_TRACING="$RayTracing";STARFOX_TEST_REFLECTIVE_SURFACES="$Reflections";
        STARFOX_TEST_ENVIRONMENT_0="$GroundEnabled";STARFOX_TEST_ENVIRONMENT_1="$Ground";STARFOX_TEST_ENVIRONMENT_2='0';
        STARFOX_TEST_ENVIRONMENT_3="$Sky";STARFOX_TRACE_GPU_RAYS='1';
        STARFOX_TEST_PRESSES=$Presses;
        STARFOX_TEST_PRESS_FRAMES="$PressFrames";
        STARFOX_CAPTURE_PRESENTATION_SEQUENCE='1';STARFOX_CAPTURE_PRESENTATION_INTERVAL='4';
        STARFOX_CAPTURE_PRESENTATION_PATH=(Join-Path $output 'lava.bmp')
    }
    if($StereoReticleDepth -gt 0) { $settings.STARFOX_TEST_STEREO_RETICLE_DEPTH="$StereoReticleDepth" }
    if($D3d11){$settings.STARFOX_TEST_D3D11_GPU='1'}
    if($DlssAdapter){$settings.STARFOX_DLSS_ADAPTER=[IO.Path]::GetFullPath($DlssAdapter)}
    if($DlssBinaries){$settings.STARFOX_DLSS_BINARIES=[IO.Path]::GetFullPath($DlssBinaries)}
    if($CameraBank -ne 0){$settings.STARFOX_TEST_CAMERA_BANK="$CameraBank";$settings.STARFOX_TRACE_GPU='1'}
    if($CameraResponse){$settings.STARFOX_TEST_CAMERA_RESPONSE="$CameraResponse";$settings.STARFOX_TRACE_GPU='1'}
    if($CameraOffFrame -ge 0){$settings.STARFOX_TEST_CAMERA_OFF_FRAME="$CameraOffFrame"}
    if($CameraOnFrame -ge 0){$settings.STARFOX_TEST_CAMERA_ON_FRAME="$CameraOnFrame"}
    if($GodMode -ge 0){$settings.STARFOX_TEST_GOD_MODE="$GodMode"}
    if($TraceObjects){$settings.STARFOX_TRACE_OBJECTS='1';$settings.STARFOX_TRACE_RENDER_STATE='1'}
    if($VolumetricFog){$settings.STARFOX_TEST_VOLUMETRIC_FOG='1';$settings.STARFOX_TRACE_GPU='1'}
    if($MotionBlur){$settings.STARFOX_TEST_MOTION_BLUR_CAPTURE=(Join-Path $output 'motion-blur.bmp');$settings.STARFOX_TRACE_GPU='1'}
    if($LiveMotionBlur){$settings.STARFOX_TEST_MOTION_BLUR_LIVE='1';$settings.STARFOX_TRACE_GPU='1'}
    if($FailStereo){$settings.STARFOX_TEST_FAIL_STEREO_PRESENT='1';$settings.STARFOX_TRACE_GPU='1'}
    if($FailStereoAfterLeft){$settings.STARFOX_TEST_FAIL_STEREO_AFTER_LEFT='1';$settings.STARFOX_TRACE_GPU='1'}
    if($Paced){$settings.Remove('STARFOX_TEST_UNPACED')}
    if($Taa){$settings.STARFOX_TEST_TAA='1';$settings.STARFOX_TRACE_GPU='1'}
    if($ExMenu -ge 0){$settings.STARFOX_TEST_EX_MENU_BACKGROUND="$ExMenu"}
    if($MenuPreview){$settings.STARFOX_TEST_MENU_PREVIEW='1'}
    if($Profile) {
        $settings.STARFOX_TRACE_GPU_PASS_COST='1'
        $settings.STARFOX_TRACE_GPU_PASS_COST_ALL='1'
        $settings.STARFOX_TRACE_SCENE_COST='1'
        $settings.STARFOX_CAPTURE_PRESENTATION_INTERVAL="$Frames"
    }
    $settings.STARFOX_TEST_FSR1_SELECTION="$Fsr1"
    if($GpuBackend -or $Fsr1 -or $Dlss -or $AaQuality -or $Stereo){$settings.STARFOX_TRACE_GPU='1'}
    foreach($entry in $settings.GetEnumerator()) {Set-Item -LiteralPath "Env:$($entry.Key)" -Value $entry.Value}
    $log=Join-Path $output 'runtime.log'
    $runtimeArguments=if($Experience -eq 'EX') {"tmp/runtime-inputs/starfox-ex/SFES.SFC assets/symbols/starfox-ex.txt $Stage"} else {"upstream-ultrastarfox/SF.sfc assets/symbols/ultrastarfox.txt $Stage"}
    $process=Start-Process $Binary -ArgumentList $runtimeArguments -WindowStyle Hidden -PassThru -RedirectStandardError $log
    $handle=$process.Handle
    if(!$process.WaitForExit(60000)) {throw "Capture still running: PID $($process.Id), $log"}
    if($process.ExitCode -ne 0) {throw "Capture failed: $log"}
    if(!(Test-Path -LiteralPath (Join-Path $output 'lava.bmp'))) {throw 'Missing final lava capture'}
    if($Stereo -and !$FailStereo -and !$FailStereoAfterLeft -and !(Select-String -LiteralPath $log -SimpleMatch 'stereo presented:' -Quiet)) {
        throw 'Requested stereo output did not present; inspect runtime.log'
    }
    if($FailStereoAfterLeft -and !(Select-String -LiteralPath $log -SimpleMatch 'injected failure after left eye' -Quiet)) {
        throw 'Stereo did not reach the left-eye failure injection; inspect runtime.log'
    }
    if($MotionBlur -and (!(Test-Path -LiteralPath (Join-Path $output 'motion-blur.bmp')) -or
        !(Select-String -LiteralPath $log -Pattern 'motion-blur-reference: moving=[1-9][0-9]* .*history=1 saved=1 changed=[1-9][0-9]* protected-changed=0' -Quiet))) {
        throw 'Motion-blur reference did not capture valid moving history; inspect runtime.log'
    }
    if($MotionBlur -and !(Select-String -LiteralPath $log -Pattern 'motion-blur-gpu: max-error=[012] protected-changed=0 saved=1' -Quiet)) {
        throw 'GPU motion-blur capture differs from reference or changed protected pixels; inspect runtime.log'
    }
    if($LiveMotionBlur -and !$FailStereo -and !$FailStereoAfterLeft -and !(Select-String -LiteralPath $log -SimpleMatch 'motion-blur-live: applied=1 history=1' -Quiet)) {
        throw 'Live motion-blur pipeline did not execute with valid history; inspect runtime.log'
    }
    if($LiveMotionBlur -and $Fsr1 -and !(Select-String -LiteralPath $log -SimpleMatch 'fsr1: scene=' -Quiet)) {
        throw 'FSR1 did not execute with live motion blur; inspect runtime.log'
    }
    if($LiveMotionBlur -and $Taa -and !(Select-String -LiteralPath $log -SimpleMatch 'taa: resolved' -Quiet)) {
        throw 'TAA did not execute with live motion blur; inspect runtime.log'
    }
    if($LiveMotionBlur -and $DepthEnhancements -and
        !(Select-String -LiteralPath $log -SimpleMatch "depth-fx: modes=$DepthEnhancements " -Quiet)) {
        throw 'Requested depth effects did not execute with live motion blur; inspect runtime.log'
    }
    if($CheckMotionPause) {
        $motionEyeCount=if($Stereo) {2} else {1}
        & (Join-Path $PSScriptRoot 'check_motion_blur_pause.ps1') -Log $log -EyeCount $motionEyeCount
    }
    if($CheckGroundShadow) {
        if($Stereo) {foreach($eye in 0,1) {
            if(!(Select-String -LiteralPath $log -Pattern "motion-ground-shadow: ready=1 hardware=1 resident_geometry=[01] eye=$eye" -Quiet)) {
                throw "Missing hardware ground-only shadow for stereo eye $eye"
            }
        }}
        if(!$Stereo -and !(Select-String -LiteralPath $log -Pattern 'motion-ground-shadow: ready=1 hardware=1 resident_geometry=1' -Quiet)) {
            throw 'No hardware ground-only mask used resident caster geometry'
        }
        if(Select-String -LiteralPath $log -Pattern 'motion-ground-shadow: ready=0|motion-blur-live: unavailable|motion-blur-live: applied=0' -Quiet) {
            throw 'Ground-shadow exposure failed or declined a frame; inspect runtime.log'
        }
    }
    if($CheckGroundReflection) {
        if($Stereo) {foreach($eye in 0,1) {
            if(!(Select-String -LiteralPath $log -SimpleMatch "motion-ground-reflection: ready=1 eye=$eye" -Quiet)) {
                throw "Missing independent ground reflection for stereo eye $eye"
            }
        }}
        if(!(Select-String -LiteralPath $log -SimpleMatch 'motion-ground-reflection: ready=1' -Quiet)) {
            throw 'No independent ground reflection reached the exposure path'
        }
        if(Select-String -LiteralPath $log -Pattern 'motion-ground-reflection: ready=0|motion-blur-live: unavailable|motion-blur-live: applied=0' -Quiet) {
            throw 'Ground-reflection exposure failed or declined a frame; inspect runtime.log'
        }
    }
    if($CheckParticleShutter) {
        if(!(Select-String -LiteralPath $log -Pattern 'particle-shutter-live: count=[1-9][0-9]* history=1' -Quiet)) {
            throw 'No live particles reached valid-history joint exposure'
        }
        if(Select-String -LiteralPath $log -Pattern 'motion-blur-live: unavailable|motion-blur-live: applied=0|stereo failure:' -Quiet) {
            throw 'Particle exposure declined or failed a frame; inspect runtime.log'
        }
    }
    if($LiveMotionBlur -and $Stereo -and !$FailStereo -and !$FailStereoAfterLeft) {
        foreach($eyeSlot in 1,2) {
            if(!(Select-String -LiteralPath $log -SimpleMatch "motion-blur-live: applied=1 history=1 slot=$eyeSlot" -Quiet)) {
                throw "Live motion blur did not execute for stereo eye slot $eyeSlot; inspect runtime.log"
            }
        }
    }
    if($LiveMotionBlur -and $FailStereoAfterLeft) {
        $failedPairs=@(Select-String -LiteralPath $log -SimpleMatch 'injected failure after left eye').Count
        if($RayTracing -and $Reflections) {
            $monoReflections=@(Select-String -LiteralPath $log -SimpleMatch 'stereo-fallback-reflections: ready=1').Count
            if($failedPairs -ne $monoReflections -or !$monoReflections) {
                throw 'Failed stereo pair did not regenerate every mono reflection frame'
            }
        }
        $identityFallbacks=@(Select-String -LiteralPath $log -SimpleMatch 'motion-blur-live: applied=1 history=0 slot=0').Count
        if($failedPairs -ne $identityFallbacks -or
            (Select-String -LiteralPath $log -SimpleMatch 'motion-blur-live: applied=1 history=1 slot=0' -Quiet)) {
            throw 'Failed stereo pair leaked motion history into the mono fallback'
        }
    }
    if($LiveMotionBlur -and $CameraBank -ne 0 -and
        !(Select-String -LiteralPath $log -SimpleMatch 'camera-response: resident world/HUD=1' -Quiet)) {
        throw 'Camera response did not execute with live motion blur; inspect runtime.log'
    }
    if($LiveMotionBlur -and $VolumetricFog -and
        !(Select-String -LiteralPath $log -SimpleMatch 'motion-blur-fog-underlay: ready=1' -Quiet)) {
        throw 'Background fog did not execute with live motion blur; inspect runtime.log'
    }
    if($Dlss -and !(Select-String -LiteralPath $log -SimpleMatch 'dlss-gameplay: evaluated' -Quiet)) {
        throw 'DLSS did not evaluate; inspect runtime.log'
    }
    # ray-water is emitted by the GPU liquid dispatcher, not the software
    # environment/reflection path. Do not require GPU evidence in a CPU capture.
    if($Renderer -eq 'GPU' -and $RayTracing -and $GroundEnabled -and $Ground -ge 5 -and !(Select-String -LiteralPath $log -SimpleMatch 'ray-water=1' -Quiet)) {
        throw 'Ray-traced liquid did not execute; inspect runtime.log'
    }
    if($CheckTemporalSurfaces -and !(Select-String -LiteralPath $log -SimpleMatch 'taa-surfaces: aligned lighting/depth' -Quiet)) {
        throw 'TAA lighting/depth surface alignment did not execute; inspect runtime.log'
    }
    if($GpuBackend -and !(Select-String -LiteralPath $log -Pattern ("(?:driver=|SDL GPU compute: )"+[regex]::Escape($GpuBackend)+"(?:\s|$)") -Quiet)) {
        throw "Requested GPU backend $GpuBackend was not confirmed; inspect runtime.log"
    }
    Write-Output "Lava capture: $output"
} finally {
    Get-ChildItem Env: | Where-Object {$_.Name -match '^(STARFOX_|SDL_AUDIODRIVER$|SDL_GPU_DRIVER$)'} | ForEach-Object {Remove-Item -LiteralPath "Env:$($_.Name)"}
    foreach($entry in $saved.GetEnumerator()) {Set-Item -LiteralPath "Env:$($entry.Key)" -Value $entry.Value}
}
