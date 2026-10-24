# SPDX-License-Identifier: GPL-3.0-or-later
# Play a MIDI file into the real U-220 and record it at the same time.  See HwCapture.cs.
param(
    [Parameter(Mandatory = $true)] [string]$Midi,
    [Parameter(Mandatory = $true)] [string]$Out,
    [string]$MidiOut = 'MIDIFACE 2x2',
    [string]$WaveIn = 'IN 3-4',
    [double]$Lead = 0.5,
    [double]$Tail = 3.0
)
if (-not ('HwCapture' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'HwCapture.cs')
}
[HwCapture]::Run((Resolve-Path $Midi).Path, [IO.Path]::GetFullPath($Out), $MidiOut, $WaveIn, $Lead, $Tail)
