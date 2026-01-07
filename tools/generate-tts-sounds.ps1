# Generate TTS sound files for menu system (optimized)
# Usage: .\tools\generate-tts-sounds.ps1
#
# Requires: Windows 10/11 (uses built-in SAPI TTS)
# Output: WAV files in tools/tts_wav/ and C headers in main/sounds/menu/

param(
    [string]$OutputDir = "tools\tts_wav",
    [string]$HeaderDir = "main\sounds\menu",
    [int]$SampleRate = 11025,  # Lower sample rate for smaller files
    [int]$Speed = 3,           # -10 to 10, higher = faster
    [int]$SilenceThreshold = 5 # Threshold for silence detection (0-127)
)

# Create output directories
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$wavDir = Join-Path $projectDir $OutputDir
$hdrDir = Join-Path $projectDir $HeaderDir

New-Item -ItemType Directory -Force -Path $wavDir | Out-Null
New-Item -ItemType Directory -Force -Path $hdrDir | Out-Null

# Initialize SAPI
Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer

# Try to use a clear voice
$voices = $synth.GetInstalledVoices() | Where-Object { $_.Enabled }
$preferredVoice = $voices | Where-Object { $_.VoiceInfo.Name -like "*Zira*" -or $_.VoiceInfo.Name -like "*David*" } | Select-Object -First 1
if ($preferredVoice) {
    $synth.SelectVoice($preferredVoice.VoiceInfo.Name)
    Write-Host "Using voice: $($preferredVoice.VoiceInfo.Name)"
} else {
    Write-Host "Using default voice: $($synth.Voice.Name)"
}

# Sound definitions: name -> text to speak
$sounds = @{
    # Menu navigation
    "menu_enter" = "Menu"
    "menu_back" = "Back"
    "menu_confirm" = "OK"
    "menu_cancel" = "Cancel"

    # Categories
    "cat_volume" = "Volume"
    "cat_profile" = "Profile"
    "cat_horn" = "Horn"
    "cat_wifi" = "WiFi"

    # Volume options
    "opt_vol_low" = "Low"
    "opt_vol_medium" = "Medium"
    "opt_vol_high" = "High"

    # Profile options
    "opt_profile_cat" = "Cat"
    "opt_profile_unimog" = "Unimog"
    "opt_profile_man" = "Man"

    # Generic options (reusable)
    "opt_on" = "On"
    "opt_off" = "Off"

    # Categories (continued)
    "cat_steering" = "Steering"

    # Horn options
    "opt_horn_truck" = "Truck"
    "opt_horn_mantge" = "Man T G E"
    "opt_horn_cucaracha" = "La Cucaracha"
    "opt_horn_2tone" = "Two Tone"
    "opt_horn_dixie" = "Dixie"
    "opt_horn_peterbilt" = "Peterbilt"
    "opt_horn_outlaw" = "Outlaw"
}

function Convert-WavTo8Bit {
    param([string]$InputFile, [int]$TargetRate, [int]$Threshold)

    # Read WAV file
    $bytes = [System.IO.File]::ReadAllBytes($InputFile)

    # Parse WAV header
    $channels = [BitConverter]::ToInt16($bytes, 22)
    $sampleRate = [BitConverter]::ToInt32($bytes, 24)
    $bitsPerSample = [BitConverter]::ToInt16($bytes, 34)
    $dataStart = 44  # Standard WAV header size

    # Find "data" chunk
    for ($i = 12; $i -lt $bytes.Length - 8; $i++) {
        if ($bytes[$i] -eq 0x64 -and $bytes[$i+1] -eq 0x61 -and $bytes[$i+2] -eq 0x74 -and $bytes[$i+3] -eq 0x61) {
            $dataStart = $i + 8
            break
        }
    }

    # Extract samples and convert to mono 8-bit
    $samples = [System.Collections.ArrayList]::new()
    $bytesPerSample = $bitsPerSample / 8
    $frameSize = $channels * $bytesPerSample

    for ($i = $dataStart; $i -lt $bytes.Length - $frameSize + 1; $i += $frameSize) {
        # Read sample (handle 16-bit)
        if ($bitsPerSample -eq 16) {
            $sample = [BitConverter]::ToInt16($bytes, $i)
            # Convert to 8-bit signed (-128 to 127)
            $sample8 = [math]::Round($sample / 256)
        } else {
            # 8-bit unsigned to signed
            $sample8 = $bytes[$i] - 128
        }
        [void]$samples.Add([sbyte]$sample8)
    }

    # Resample if needed
    if ($sampleRate -ne $TargetRate) {
        $ratio = $sampleRate / $TargetRate
        $newSamples = [System.Collections.ArrayList]::new()
        for ($i = 0; $i -lt [math]::Floor($samples.Count / $ratio); $i++) {
            $srcIdx = [math]::Floor($i * $ratio)
            [void]$newSamples.Add($samples[$srcIdx])
        }
        $samples = $newSamples
    }

    # Trim leading silence
    $startIdx = 0
    for ($i = 0; $i -lt $samples.Count; $i++) {
        if ([math]::Abs($samples[$i]) -gt $Threshold) {
            # Go back a tiny bit for attack
            $startIdx = [math]::Max(0, $i - [math]::Floor($TargetRate * 0.02))
            break
        }
    }

    # Trim trailing silence
    $endIdx = $samples.Count - 1
    for ($i = $samples.Count - 1; $i -ge 0; $i--) {
        if ([math]::Abs($samples[$i]) -gt $Threshold) {
            # Add a tiny bit for release
            $endIdx = [math]::Min($samples.Count - 1, $i + [math]::Floor($TargetRate * 0.05))
            break
        }
    }

    # Extract trimmed samples
    $trimmedSamples = @()
    for ($i = $startIdx; $i -le $endIdx; $i++) {
        $trimmedSamples += $samples[$i]
    }

    return $trimmedSamples
}

function Export-ToHeader {
    param([string]$Name, [sbyte[]]$Samples, [int]$SampleRate, [string]$OutputFile)

    $varName = "menu_$Name"
    $sb = [System.Text.StringBuilder]::new()

    [void]$sb.AppendLine("// TTS sound: $Name")
    [void]$sb.AppendLine("// Generated by generate-tts-sounds.ps1")
    [void]$sb.AppendLine("// Optimized: ${SampleRate}Hz, silence trimmed")
    [void]$sb.AppendLine("const unsigned int ${varName}SampleRate = $SampleRate;")
    [void]$sb.AppendLine("const unsigned int ${varName}SampleCount = $($Samples.Length);")
    [void]$sb.AppendLine("const signed char ${varName}Samples[] = {")

    # Write samples in rows of 16
    for ($i = 0; $i -lt $Samples.Length; $i += 16) {
        $row = @()
        for ($j = 0; $j -lt 16 -and ($i + $j) -lt $Samples.Length; $j++) {
            $row += $Samples[$i + $j]
        }
        $line = ($row -join ", ")
        if (($i + 16) -lt $Samples.Length) {
            $line += ","
        }
        [void]$sb.AppendLine($line)
    }

    [void]$sb.AppendLine("};")

    [System.IO.File]::WriteAllText($OutputFile, $sb.ToString())
}

Write-Host "`nGenerating TTS sounds (optimized)..."
Write-Host "Sample rate: ${SampleRate}Hz (was 22050Hz)"
Write-Host "Silence trimming enabled"
Write-Host "======================================`n"

$totalOriginal = 0
$totalOptimized = 0

foreach ($sound in $sounds.GetEnumerator()) {
    $name = $sound.Key
    $text = $sound.Value
    $wavFile = Join-Path $wavDir "$name.wav"
    $hdrFile = Join-Path $hdrDir "$name.h"

    Write-Host "Generating: $name ('$text')..." -NoNewline

    # Generate WAV with SAPI
    $synth.Rate = $Speed
    $synth.SetOutputToWaveFile($wavFile)
    $synth.Speak($text)
    $synth.SetOutputToNull()

    # Convert to 8-bit, resample, and trim silence
    $samples = Convert-WavTo8Bit -InputFile $wavFile -TargetRate $SampleRate -Threshold $SilenceThreshold

    if ($samples.Length -gt 0) {
        Export-ToHeader -Name $name -Samples $samples -SampleRate $SampleRate -OutputFile $hdrFile
        $duration = [math]::Round($samples.Length / $SampleRate, 2)
        $sizeKB = [math]::Round($samples.Length / 1024, 1)
        Write-Host " $($samples.Length) samples, ${duration}s, ${sizeKB}KB"
        $totalOptimized += $samples.Length
    } else {
        Write-Host " ERROR: No samples generated" -ForegroundColor Red
    }
}

$synth.Dispose()

Write-Host "`n======================================"
Write-Host "Done! Generated $($sounds.Count) sound files."
Write-Host "Total size: $([math]::Round($totalOptimized / 1024, 1))KB"
Write-Host "WAV files: $wavDir"
Write-Host "Headers:   $hdrDir"
